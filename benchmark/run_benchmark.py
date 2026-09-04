#!/usr/bin/env python3
"""Repeatable correctness and performance benchmark for the Tinyweb cloud drive."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Optional


SUMMARY_FIELDS = [
    "test_name",
    "concurrency",
    "duration_seconds",
    "total_requests",
    "successful_requests",
    "failed_requests",
    "qps",
    "latency_avg_ms",
    "latency_p50_ms",
    "latency_p95_ms",
    "latency_p99_ms",
    "latency_max_ms",
    "transfer_mib_s",
    "cpu_avg_percent",
    "cpu_peak_percent",
    "rss_avg_mib",
    "rss_peak_mib",
]

NUMERIC_MEDIAN_FIELDS = [
    "duration_seconds",
    "total_requests",
    "successful_requests",
    "failed_requests",
    "qps",
    "latency_avg_ms",
    "latency_p50_ms",
    "latency_p95_ms",
    "latency_p99_ms",
    "latency_max_ms",
    "transfer_mib_s",
    "cpu_avg_percent",
    "cpu_peak_percent",
    "rss_avg_mib",
    "rss_peak_mib",
    "socket_errors",
    "non_2xx_responses",
]

COUNT_FIELDS = {
    "total_requests",
    "successful_requests",
    "failed_requests",
    "socket_errors",
    "non_2xx_responses",
}


class BenchmarkError(RuntimeError):
    """Raised for an expected benchmark or validation failure."""


def parse_positive_int_list(value: str) -> list[int]:
    try:
        parsed = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("必须是逗号分隔的正整数") from exc
    if not parsed or any(item <= 0 for item in parsed):
        raise argparse.ArgumentTypeError("必须是逗号分隔的正整数")
    return parsed


def parse_duration_seconds(value: str) -> float:
    match = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(us|ms|s|m|h)\s*", value)
    if not match:
        raise ValueError(f"无法解析时间: {value!r}")
    number = float(match.group(1))
    factors = {"us": 1e-6, "ms": 1e-3, "s": 1.0, "m": 60.0, "h": 3600.0}
    return number * factors[match.group(2)]


def latency_to_ms(value: str) -> float:
    match = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(us|ms|s)\s*", value)
    if not match:
        raise ValueError(f"无法解析延迟: {value!r}")
    number = float(match.group(1))
    unit = match.group(2)
    if unit == "us":
        return number / 1000.0
    if unit == "s":
        return number * 1000.0
    return number


def transfer_to_mib_s(value: str) -> float:
    match = re.fullmatch(
        r"\s*([0-9]+(?:\.[0-9]+)?)\s*(B|KB|MB|GB)(?:/s)?\s*",
        value,
        re.IGNORECASE,
    )
    if not match:
        raise ValueError(f"无法解析吞吐量: {value!r}")
    number = float(match.group(1))
    unit = match.group(2).upper()
    factors = {"B": 1.0 / (1024.0 * 1024.0), "KB": 1.0 / 1024.0, "MB": 1.0, "GB": 1024.0}
    return number * factors[unit]


def _search_latency(output: str, label: str) -> Optional[float]:
    custom = re.search(rf"^TinyBench-{label}:\s*([^\s]+)\s*$", output, re.MULTILINE)
    if custom:
        return latency_to_ms(custom.group(1))
    distribution = re.search(rf"^\s*{label[1:]}%\s+([^\s]+)\s*$", output, re.MULTILINE)
    if distribution:
        return latency_to_ms(distribution.group(1))
    return None


def parse_wrk_output(output: str) -> dict[str, Any]:
    """Parse standard wrk --latency output plus TinyBench percentile markers."""
    request_match = re.search(r"([0-9]+)\s+requests\s+in\s+([^,\s]+)", output)
    qps_match = re.search(r"^Requests/sec:\s*([0-9]+(?:\.[0-9]+)?)", output, re.MULTILINE)
    latency_match = re.search(
        r"^\s*Latency\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)\s+[^\s]+\s*$",
        output,
        re.MULTILINE,
    )
    transfer_match = re.search(r"^Transfer/sec:\s*([^\s]+)", output, re.MULTILINE)
    if not request_match or not qps_match or not latency_match:
        raise ValueError("wrk 输出缺少请求数、QPS 或延迟汇总")

    total_requests = int(request_match.group(1))
    socket_errors = 0
    socket_match = re.search(
        r"Socket errors:\s*connect\s+([0-9]+),\s*read\s+([0-9]+),\s*"
        r"write\s+([0-9]+),\s*timeout\s+([0-9]+)",
        output,
    )
    if socket_match:
        socket_errors = sum(int(item) for item in socket_match.groups())
    non_2xx_match = re.search(r"Non-2xx or 3xx responses:\s*([0-9]+)", output)
    non_2xx = int(non_2xx_match.group(1)) if non_2xx_match else 0
    failed_requests = socket_errors + non_2xx

    result = {
        "duration_seconds": parse_duration_seconds(request_match.group(2)),
        "total_requests": total_requests,
        "successful_requests": max(total_requests - failed_requests, 0),
        "failed_requests": failed_requests,
        "qps": float(qps_match.group(1)),
        "latency_avg_ms": latency_to_ms(latency_match.group(1)),
        "latency_p50_ms": _search_latency(output, "P50"),
        "latency_p95_ms": _search_latency(output, "P95"),
        "latency_p99_ms": _search_latency(output, "P99"),
        "latency_max_ms": latency_to_ms(latency_match.group(3)),
        "transfer_mib_s": transfer_to_mib_s(transfer_match.group(1)) if transfer_match else None,
        "socket_errors": socket_errors,
        "non_2xx_responses": non_2xx,
    }
    if any(result[key] is None for key in ("latency_p50_ms", "latency_p95_ms", "latency_p99_ms")):
        raise ValueError("wrk 输出缺少 P50/P95/P99；请使用脚本生成的 Lua 百分位标记")
    return result


def median_runs(rows: list[dict[str, Any]]) -> dict[str, Any]:
    if not rows:
        raise ValueError("无法对空结果取中位数")
    result = dict(rows[0])
    for field in NUMERIC_MEDIAN_FIELDS:
        values = [float(row[field]) for row in rows if row.get(field) is not None]
        if not values:
            result[field] = None
            continue
        median_value: float | int = statistics.median(values)
        if field in COUNT_FIELDS:
            median_value = int(round(median_value))
        result[field] = median_value
    result["all_runs_zero_error"] = all(
        int(row.get("failed_requests", 0)) == 0 and row.get("process_ok", True) for row in rows
    )
    result["runs"] = len(rows)
    return result


def percentile(values: Iterable[float], ratio: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    rank = (len(ordered) - 1) * ratio
    lower = int(rank)
    upper = min(lower + 1, len(ordered) - 1)
    weight = rank - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def parse_auth_output(output: str, concurrency: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    stage_names = {"注册": "auth_register", "登录": "auth_login"}
    pattern = re.compile(r"=====\s*(注册|登录)\s*汇总\s*=====(.*?)(?=\n=====|\Z)", re.DOTALL)
    for match in pattern.finditer(output):
        stage, body = match.groups()

        def integer(label: str) -> int:
            found = re.search(rf"{label}\s*:\s*([0-9]+)", body)
            if not found:
                raise ValueError(f"批量认证输出缺少 {label}")
            return int(found.group(1))

        def number(label: str) -> float:
            found = re.search(rf"{label}\s*:\s*([0-9]+(?:\.[0-9]+)?)", body)
            if not found:
                raise ValueError(f"批量认证输出缺少 {label}")
            return float(found.group(1))

        latency = re.search(
            r"延迟\(ms\)\s*:\s*avg=([0-9.]+),\s*p50=([0-9.]+),\s*"
            r"p95=([0-9.]+),\s*p99=([0-9.]+),\s*max=([0-9.]+)",
            body,
        )
        if not latency:
            raise ValueError("批量认证输出缺少延迟分位数")
        rows.append(
            {
                "test_name": stage_names[stage],
                "concurrency": concurrency,
                "duration_seconds": number("阶段耗时"),
                "total_requests": integer("总请求数"),
                "successful_requests": integer("成功数"),
                "failed_requests": integer("失败数"),
                "qps": number("吞吐量"),
                "latency_avg_ms": float(latency.group(1)),
                "latency_p50_ms": float(latency.group(2)),
                "latency_p95_ms": float(latency.group(3)),
                "latency_p99_ms": float(latency.group(4)),
                "latency_max_ms": float(latency.group(5)),
                "transfer_mib_s": None,
                "all_runs_zero_error": integer("失败数") == 0,
                "runs": 1,
            }
        )
    if len(rows) != 2:
        raise ValueError("无法从批量认证输出解析注册和登录两个阶段")
    return rows


def read_process_sample(pid: int) -> tuple[int, float]:
    stat_text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
    tail = stat_text[stat_text.rfind(")") + 2 :].split()
    process_ticks = int(tail[11]) + int(tail[12])
    status_text = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
    rss_match = re.search(r"^VmRSS:\s*([0-9]+)\s*kB", status_text, re.MULTILINE)
    if not rss_match:
        raise OSError("/proc 状态中没有 VmRSS")
    return process_ticks, int(rss_match.group(1)) / 1024.0


class ResourceSampler:
    def __init__(self, pid: Optional[int], enabled: bool, interval: float = 0.25) -> None:
        self.pid = pid
        self.enabled = enabled and pid is not None
        self.interval = interval
        self.cpu_samples: list[float] = []
        self.rss_samples: list[float] = []
        self.error: Optional[str] = None
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        if not self.enabled:
            return
        self._thread = threading.Thread(target=self._run, name="resource-sampler", daemon=True)
        self._thread.start()

    def _run(self) -> None:
        assert self.pid is not None
        ticks_per_second = os.sysconf("SC_CLK_TCK")
        previous_ticks: Optional[int] = None
        previous_time: Optional[float] = None
        while not self._stop.is_set():
            try:
                current_time = time.monotonic()
                current_ticks, rss_mib = read_process_sample(self.pid)
                self.rss_samples.append(rss_mib)
                if previous_ticks is not None and previous_time is not None:
                    elapsed = current_time - previous_time
                    if elapsed > 0:
                        cpu = (current_ticks - previous_ticks) / ticks_per_second / elapsed * 100.0
                        self.cpu_samples.append(max(cpu, 0.0))
                previous_ticks = current_ticks
                previous_time = current_time
            except (OSError, ValueError) as exc:
                self.error = str(exc)
                return
            self._stop.wait(self.interval)

    def stop(self) -> dict[str, Optional[float]]:
        if not self.enabled:
            return empty_resource_metrics()
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2.0)
        return {
            "cpu_avg_percent": statistics.fmean(self.cpu_samples) if self.cpu_samples else None,
            "cpu_peak_percent": max(self.cpu_samples) if self.cpu_samples else None,
            "rss_avg_mib": statistics.fmean(self.rss_samples) if self.rss_samples else None,
            "rss_peak_mib": max(self.rss_samples) if self.rss_samples else None,
        }


def empty_resource_metrics() -> dict[str, None]:
    return {
        "cpu_avg_percent": None,
        "cpu_peak_percent": None,
        "rss_avg_mib": None,
        "rss_peak_mib": None,
    }


@dataclass
class HttpResult:
    status: int
    headers: dict[str, str]
    body_path: Path
    stderr: str
    returncode: int


class CurlClient:
    def __init__(self, base_url: str, cookie_jar: Path, temp_dir: Path, timeout: float = 120.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.cookie_jar = cookie_jar
        self.temp_dir = temp_dir
        self.timeout = timeout
        self._counter = 0
        self._lock = threading.Lock()

    def _next_paths(self) -> tuple[Path, Path]:
        with self._lock:
            self._counter += 1
            counter = self._counter
        return self.temp_dir / f"headers_{counter}.txt", self.temp_dir / f"body_{counter}.bin"

    def request(
        self,
        method: str,
        path: str,
        *,
        fields: Optional[dict[str, Any]] = None,
        upload_path: Optional[Path] = None,
        upload_name: Optional[str] = None,
        extra_headers: Optional[dict[str, str]] = None,
        body_path: Optional[Path] = None,
        update_cookies: bool = False,
    ) -> HttpResult:
        header_path, default_body_path = self._next_paths()
        target_body = body_path or default_body_path
        command = [
            "curl",
            "--silent",
            "--show-error",
            "--request",
            method,
            "--max-time",
            str(self.timeout),
            "--dump-header",
            str(header_path),
            "--output",
            str(target_body),
            "--write-out",
            "%{http_code}",
            "--cookie",
            str(self.cookie_jar),
        ]
        if update_cookies:
            command.extend(["--cookie-jar", str(self.cookie_jar)])
        for name, value in (extra_headers or {}).items():
            command.extend(["--header", f"{name}: {value}"])
        if upload_path is not None:
            if not upload_name:
                raise ValueError("上传时必须提供 upload_name")
            command.extend(
                ["--form", f"file=@{upload_path};filename={upload_name};type=application/octet-stream"]
            )
        elif fields is not None:
            for name, value in fields.items():
                command.extend(["--data-urlencode", f"{name}={value}"])
        command.append(self.base_url + path)
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        status_text = completed.stdout.strip()
        status = int(status_text) if status_text.isdigit() else 0
        headers = parse_headers(header_path)
        header_path.unlink(missing_ok=True)
        return HttpResult(status, headers, target_body, completed.stderr.strip(), completed.returncode)


def parse_headers(path: Path) -> dict[str, str]:
    headers: dict[str, str] = {}
    if not path.exists():
        return headers
    for line in path.read_text(encoding="iso-8859-1").splitlines():
        if ":" not in line:
            continue
        name, value = line.split(":", 1)
        headers[name.strip().lower()] = value.strip()
    return headers


def read_small_body(result: HttpResult, limit: int = 4 * 1024 * 1024) -> bytes:
    if not result.body_path.exists():
        return b""
    if result.body_path.stat().st_size > limit:
        raise BenchmarkError("响应正文超过功能检查上限")
    return result.body_path.read_bytes()


def ensure_http_success(result: HttpResult, stage: str, exact_status: Optional[int] = None) -> None:
    expected = result.status == exact_status if exact_status is not None else 200 <= result.status < 300
    if result.returncode != 0 or not expected:
        detail = result.stderr or f"HTTP {result.status}"
        raise BenchmarkError(f"{stage}失败: {detail}")


def extract_session(cookie_jar: Path) -> Optional[str]:
    if not cookie_jar.exists():
        return None
    for raw_line in cookie_jar.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line
        if line.startswith("#HttpOnly_"):
            line = line[len("#HttpOnly_") :]
        elif line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) >= 7 and fields[-2] == "session" and fields[-1]:
            return fields[-1]
    return None


def load_file_list(client: CurlClient) -> list[dict[str, Any]]:
    result = client.request("GET", "/file")
    ensure_http_success(result, "GET /file")
    try:
        payload = json.loads(read_small_body(result).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BenchmarkError("GET /file 未返回合法 JSON") from exc
    files = payload.get("files") if isinstance(payload, dict) else None
    if not isinstance(files, list) or any(not isinstance(item, dict) for item in files):
        raise BenchmarkError("GET /file JSON 中缺少 files 数组")
    return files


def generate_test_file(path: Path, size_bytes: int) -> str:
    block = bytes(range(256)) * 4096
    digest = hashlib.sha256()
    remaining = size_bytes
    with path.open("wb") as handle:
        while remaining:
            current = block[: min(len(block), remaining)]
            handle.write(current)
            digest.update(current)
            remaining -= len(current)
    return digest.hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def compare_file_segment(source: Path, actual: Path, start: int, length: int) -> bool:
    if not actual.exists() or actual.stat().st_size != length:
        return False
    remaining = length
    with source.open("rb") as expected_handle, actual.open("rb") as actual_handle:
        expected_handle.seek(start)
        while remaining:
            chunk_size = min(1024 * 1024, remaining)
            if expected_handle.read(chunk_size) != actual_handle.read(chunk_size):
                return False
            remaining -= chunk_size
        return actual_handle.read(1) == b""


def validate_range(
    client: CurlClient,
    download_path: str,
    source_path: Path,
    file_size: int,
    range_value: str,
    expected_start: int,
    expected_end: int,
) -> None:
    target = client.temp_dir / f"range_{expected_start}_{expected_end}.bin"
    result = client.request(
        "GET",
        download_path,
        extra_headers={"Range": range_value},
        body_path=target,
    )
    ensure_http_success(result, f"Range {range_value}", exact_status=206)
    expected_length = expected_end - expected_start + 1
    expected_content_range = f"bytes {expected_start}-{expected_end}/{file_size}"
    if result.headers.get("content-range") != expected_content_range:
        raise BenchmarkError(
            f"Range {range_value} 的 Content-Range 错误: "
            f"{result.headers.get('content-range')!r}"
        )
    try:
        content_length = int(result.headers.get("content-length", ""))
    except ValueError as exc:
        raise BenchmarkError(f"Range {range_value} 的 Content-Length 非法") from exc
    if content_length != expected_length:
        raise BenchmarkError(
            f"Range {range_value} 的 Content-Length 应为 {expected_length}，实际为 {content_length}"
        )
    if not compare_file_segment(source_path, target, expected_start, expected_length):
        raise BenchmarkError(f"Range {range_value} 响应正文与源文件不一致")


def make_lua_script(path: Path, session: Optional[str]) -> None:
    lines = ['wrk.method = "GET"']
    if session:
        escaped = session.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'wrk.headers["Cookie"] = "session={escaped}"')
    lines.extend(
        [
            "done = function(summary, latency, requests)",
            '  io.write(string.format("TinyBench-P50: %.3fus\\n", latency:percentile(50.0)))',
            '  io.write(string.format("TinyBench-P95: %.3fus\\n", latency:percentile(95.0)))',
            '  io.write(string.format("TinyBench-P99: %.3fus\\n", latency:percentile(99.0)))',
            "end",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_wrk_matrix(
    *,
    test_name: str,
    url: str,
    connections: list[int],
    threads: int,
    duration: int,
    runs: int,
    warmup: int,
    session: Optional[str],
    temp_dir: Path,
    raw_dir: Path,
    server_pid: Optional[int],
    collect_resources: bool,
    errors: list[str],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    script_path = temp_dir / f"{test_name}.lua"
    make_lua_script(script_path, session)
    for concurrency in connections:
        effective_threads = min(threads, concurrency)
        common = [
            "wrk",
            "--latency",
            f"-t{effective_threads}",
            f"-c{concurrency}",
            "-s",
            str(script_path),
        ]
        warmup_command = common + [f"-d{warmup}s", url]
        warmup_result = subprocess.run(warmup_command, capture_output=True, text=True, check=False)
        if warmup_result.returncode != 0:
            message = f"{test_name} 并发 {concurrency} 预热失败: {warmup_result.stderr.strip()}"
            errors.append(message)
            continue
        individual: list[dict[str, Any]] = []
        for run_number in range(1, runs + 1):
            command = common + [f"-d{duration}s", url]
            sampler = ResourceSampler(server_pid, collect_resources)
            sampler.start()
            completed = subprocess.run(command, capture_output=True, text=True, check=False)
            resources = sampler.stop()
            output = completed.stdout + ("\n" + completed.stderr if completed.stderr else "")
            raw_path = raw_dir / f"{test_name}_c{concurrency}_run{run_number}.txt"
            raw_path.write_text(output, encoding="utf-8")
            try:
                parsed = parse_wrk_output(output)
            except ValueError as exc:
                errors.append(f"{test_name} 并发 {concurrency} 第 {run_number} 次解析失败: {exc}")
                continue
            parsed.update(resources)
            parsed.update(
                {
                    "test_name": test_name,
                    "concurrency": concurrency,
                    "process_ok": completed.returncode == 0,
                }
            )
            if completed.returncode != 0:
                errors.append(f"{test_name} 并发 {concurrency} 第 {run_number} 次 wrk 退出码异常")
            individual.append(parsed)
        if len(individual) == runs:
            rows.append(median_runs(individual))
        else:
            errors.append(f"{test_name} 并发 {concurrency} 没有得到完整的 {runs} 次有效结果")
    return rows


def run_upload_case(
    *,
    client: CurlClient,
    source_path: Path,
    concurrency: int,
    timestamp: str,
    raw_dir: Path,
    server_pid: Optional[int],
    collect_resources: bool,
) -> tuple[dict[str, Any], set[str]]:
    names = {f"benchmark_{timestamp}_upload_c{concurrency}_{index}.bin" for index in range(concurrency)}

    def upload(name: str) -> dict[str, Any]:
        body_path = client.temp_dir / f"upload_response_{name}.bin"
        command = [
            "curl",
            "--silent",
            "--show-error",
            "--request",
            "POST",
            "--max-time",
            str(max(client.timeout, 600.0)),
            "--output",
            str(body_path),
            "--write-out",
            "%{http_code}\t%{time_total}\t%{size_upload}",
            "--cookie",
            str(client.cookie_jar),
            "--form",
            f"file=@{source_path};filename={name};type=application/octet-stream",
            client.base_url + "/file",
        ]
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        fields = completed.stdout.strip().split("\t")
        try:
            status = int(fields[0])
            latency = float(fields[1])
            uploaded_bytes = float(fields[2])
        except (ValueError, IndexError):
            status, latency, uploaded_bytes = 0, 0.0, 0.0
        return {
            "name": name,
            "status": status,
            "latency_seconds": latency,
            "curl_size_upload_bytes": uploaded_bytes,
            "returncode": completed.returncode,
            "stderr": completed.stderr.strip(),
            "success": completed.returncode == 0 and 200 <= status < 300,
        }

    sampler = ResourceSampler(server_pid, collect_resources)
    sampler.start()
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
        results = list(executor.map(upload, sorted(names)))
    wall_seconds = time.monotonic() - started
    resources = sampler.stop()
    raw_path = raw_dir / f"upload_c{concurrency}.json"
    raw_path.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    latencies_ms = [item["latency_seconds"] * 1000.0 for item in results]
    successes = [item for item in results if item["success"]]
    total_uploaded = source_path.stat().st_size * len(successes)
    row: dict[str, Any] = {
        "test_name": "upload_file",
        "concurrency": concurrency,
        "duration_seconds": wall_seconds,
        "total_requests": len(results),
        "successful_requests": len(successes),
        "failed_requests": len(results) - len(successes),
        "qps": len(results) / wall_seconds if wall_seconds > 0 else 0.0,
        "latency_avg_ms": statistics.fmean(latencies_ms) if latencies_ms else 0.0,
        "latency_p50_ms": percentile(latencies_ms, 0.50),
        "latency_p95_ms": percentile(latencies_ms, 0.95),
        "latency_p99_ms": percentile(latencies_ms, 0.99),
        "latency_max_ms": max(latencies_ms) if latencies_ms else 0.0,
        "transfer_mib_s": total_uploaded / (1024.0 * 1024.0) / wall_seconds if wall_seconds > 0 else 0.0,
        "total_bytes": total_uploaded,
        "all_runs_zero_error": len(successes) == len(results),
        "runs": 1,
    }
    row.update(resources)
    return row, names


def attach_resources(rows: list[dict[str, Any]], resources: dict[str, Optional[float]]) -> None:
    for row in rows:
        row.update(resources)


def run_auth_benchmarks(
    *,
    repo_root: Path,
    base_url: str,
    password: str,
    count: int,
    workers: list[int],
    timestamp: str,
    raw_dir: Path,
    server_pid: Optional[int],
    collect_resources: bool,
    errors: list[str],
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    script = repo_root / "batch_register_login.py"
    for worker_count in workers:
        failure_csv = raw_dir / f"auth_failures_w{worker_count}.csv"
        command = [
            sys.executable,
            str(script),
            "--base-url",
            base_url,
            "--count",
            str(count),
            "--workers",
            str(worker_count),
            "--pause",
            "0",
            "--register-path",
            "/register.html",
            "--login-path",
            "/login.html",
            "--register-success-marker",
            "欢迎回来",
            "--login-success-marker",
            "欢迎回来",
            "--password",
            password,
            "--prefix",
            f"benchmark_{timestamp}_w{worker_count}",
            "--failures-csv",
            str(failure_csv),
        ]
        sampler = ResourceSampler(server_pid, collect_resources)
        sampler.start()
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        resources = sampler.stop()
        combined = completed.stdout + ("\n" + completed.stderr if completed.stderr else "")
        sanitized = combined.replace(password, "[REDACTED]")
        (raw_dir / f"auth_w{worker_count}.txt").write_text(sanitized, encoding="utf-8")
        if failure_csv.exists():
            failure_text = failure_csv.read_text(encoding="utf-8", errors="replace")
            failure_csv.write_text(failure_text.replace(password, "[REDACTED]"), encoding="utf-8")
        try:
            parsed_rows = parse_auth_output(combined, worker_count)
        except ValueError as exc:
            errors.append(f"认证压测 workers={worker_count} 解析失败: {exc}")
            continue
        attach_resources(parsed_rows, resources)
        if completed.returncode != 0:
            errors.append(f"认证压测 workers={worker_count} 存在失败请求，详见脱敏后的 raw 输出")
            for row in parsed_rows:
                row["all_runs_zero_error"] = False
        rows.extend(parsed_rows)
    return rows


def cleanup_created_files(client: CurlClient, names: set[str]) -> dict[str, Any]:
    result: dict[str, Any] = {"requested": len(names), "deleted": 0, "failed": [], "status": "completed"}
    if not names:
        return result
    try:
        files = load_file_list(client)
    except BenchmarkError as exc:
        result["status"] = "failed"
        result["failed"].append(str(exc))
        return result
    for item in files:
        name = item.get("file_name")
        file_id = item.get("file_id")
        if name not in names or file_id is None:
            continue
        response = client.request("POST", "/file/delete", fields={"file_id": file_id})
        if response.returncode == 0 and 200 <= response.status < 300:
            result["deleted"] += 1
        else:
            result["failed"].append({"file_name": name, "status": response.status})
    if result["failed"]:
        result["status"] = "partial"
    return result


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_summary_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=SUMMARY_FIELDS, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({field: "" if row.get(field) is None else row.get(field) for field in SUMMARY_FIELDS})


def format_number(value: Any, digits: int = 2) -> str:
    if value is None:
        return "未采集"
    if isinstance(value, int):
        return str(value)
    return f"{float(value):.{digits}f}"


def error_rate(row: dict[str, Any]) -> float:
    total = float(row.get("total_requests") or 0)
    return float(row.get("failed_requests") or 0) / total * 100.0 if total else 0.0


def resume_eligibility(
    rows: list[dict[str, Any]], gates: dict[str, dict[str, Any]], args: argparse.Namespace
) -> tuple[bool, str]:
    if args.smoke:
        return False, "smoke 模式仅用于流程检查"
    if args.skip_auth or args.skip_upload or args.skip_download:
        return False, "本轮跳过了至少一个必需的业务场景"
    if not gates or any(item.get("status") != "passed" for item in gates.values()):
        return False, "正确性门禁未全部通过"
    required = {"static_index", "auth_register", "auth_login", "api_file_list", "upload_file", "download_file"}
    present = {row.get("test_name") for row in rows}
    if not required.issubset(present):
        return False, "性能结果不完整"
    if any(int(row.get("failed_requests") or 0) != 0 or not row.get("all_runs_zero_error", True) for row in rows):
        return False, "至少一个性能场景存在错误"
    return True, "正确性门禁全部通过，且所有汇总场景错误率为 0"


def write_report(
    path: Path,
    environment: dict[str, Any],
    gates: dict[str, dict[str, Any]],
    rows: list[dict[str, Any]],
    errors: list[str],
    cleanup: dict[str, Any],
    args: argparse.Namespace,
) -> None:
    eligible, reason = resume_eligibility(rows, gates, args)
    lines = [
        "# Tinyweb 云盘压测报告",
        "",
        f"- 测试时间：{environment['test_time']}",
        f"- Git 提交：`{environment['git_commit']}`",
        f"- 环境：{environment['os']}，{environment['cpu_cores']} 核，{environment['memory_total_gib']:.2f} GiB",
        f"- 服务地址：`{environment['base_url']}`",
        f"- 测试文件：{environment['file_size_mib']} MiB",
        f"- 服务资源采集：{environment['resource_collection']}",
        "",
        "## 正确性门禁",
        "",
        "| 阶段 | 状态 | 说明 |",
        "|---|---|---|",
    ]
    for name, gate in gates.items():
        detail = str(gate.get("detail", "")).replace("|", "\\|")
        lines.append(f"| {name} | {gate.get('status')} | {detail} |")
    lines.extend(
        [
            "",
            "## 性能汇总（重复运行取中位数）",
            "",
            "| 场景 | 并发 | 请求数 | QPS | P50 ms | P95 ms | P99 ms | MiB/s | 错误率 | CPU 平均/峰值 | RSS 平均/峰值 MiB |",
            "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ]
    )
    if rows:
        for row in rows:
            lines.append(
                "| {test_name} | {concurrency} | {total} | {qps} | {p50} | {p95} | {p99} | "
                "{transfer} | {error:.3f}% | {cpu_avg}/{cpu_peak} | {rss_avg}/{rss_peak} |".format(
                    test_name=row["test_name"],
                    concurrency=row["concurrency"],
                    total=row["total_requests"],
                    qps=format_number(row.get("qps")),
                    p50=format_number(row.get("latency_p50_ms")),
                    p95=format_number(row.get("latency_p95_ms")),
                    p99=format_number(row.get("latency_p99_ms")),
                    transfer=format_number(row.get("transfer_mib_s")),
                    error=error_rate(row),
                    cpu_avg=format_number(row.get("cpu_avg_percent")),
                    cpu_peak=format_number(row.get("cpu_peak_percent")),
                    rss_avg=format_number(row.get("rss_avg_mib")),
                    rss_peak=format_number(row.get("rss_peak_mib")),
                )
            )
    else:
        lines.append("| 无有效性能结果 | - | - | - | - | - | - | - | - | - | - |")

    lines.extend(["", "## 简历候选描述", ""])
    if eligible:
        static_row = max(
            (row for row in rows if row["test_name"] == "static_index"),
            key=lambda row: float(row.get("qps") or 0),
        )
        download_row = max(
            (row for row in rows if row["test_name"] == "download_file"),
            key=lambda row: float(row.get("transfer_mib_s") or 0),
        )
        lines.append(
            "在 {cores} 核、{memory:.2f} GiB 环境下，Tinyweb 静态资源在 {static_c} 并发达到 "
            "{static_qps:.2f} req/s，P95 延迟 {static_p95:.2f} ms；文件下载在 {download_c} 并发达到 "
            "{download_rate:.2f} MiB/s，P95 延迟 {download_p95:.2f} ms，错误率 0%。".format(
                cores=environment["cpu_cores"],
                memory=environment["memory_total_gib"],
                static_c=static_row["concurrency"],
                static_qps=float(static_row["qps"]),
                static_p95=float(static_row["latency_p95_ms"]),
                download_c=download_row["concurrency"],
                download_rate=float(download_row["transfer_mib_s"]),
                download_p95=float(download_row["latency_p95_ms"]),
            )
        )
    else:
        lines.append(f"**本轮结果不可用于简历。** 原因：{reason}。")

    lines.extend(
        [
            "",
            "## 清理与异常",
            "",
            f"- 本轮测试文件清理：{cleanup.get('status', '未执行')}，删除 {cleanup.get('deleted', 0)}/{cleanup.get('requested', 0)}。",
        ]
    )
    if errors:
        for message in errors:
            lines.append(f"- {message}")
    else:
        lines.append("- 未记录执行异常。")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def detect_os() -> str:
    os_release = Path("/etc/os-release")
    if os_release.exists():
        match = re.search(r'^PRETTY_NAME="?([^"\n]+)"?', os_release.read_text(encoding="utf-8"), re.MULTILINE)
        if match:
            return match.group(1)
    return platform.platform()


def detect_cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        match = re.search(r"^model name\s*:\s*(.+)$", cpuinfo.read_text(encoding="utf-8"), re.MULTILINE)
        if match:
            return match.group(1).strip()
    return platform.processor() or "unknown"


def detect_memory_gib() -> float:
    meminfo = Path("/proc/meminfo").read_text(encoding="utf-8")
    match = re.search(r"^MemTotal:\s*([0-9]+)\s*kB", meminfo, re.MULTILINE)
    if not match:
        raise BenchmarkError("无法读取系统总内存")
    return int(match.group(1)) / (1024.0 * 1024.0)


def command_version(command: list[str]) -> str:
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    output = (completed.stdout or completed.stderr).strip()
    return output.splitlines()[0] if output else "unknown"


def git_commit(repo_root: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo_root, capture_output=True, text=True, check=False
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def validate_dependencies(repo_root: Path, skip_auth: bool) -> None:
    missing = [name for name in ("python3", "curl", "wrk") if shutil.which(name) is None]
    if missing:
        joined = ", ".join(missing)
        raise BenchmarkError(f"缺少依赖: {joined}。Ubuntu 可执行: sudo apt install python3 curl wrk")
    if not skip_auth and not (repo_root / "batch_register_login.py").is_file():
        raise BenchmarkError("仓库根目录缺少 batch_register_login.py，无法执行登录注册压测")


def is_local_base_url(base_url: str) -> bool:
    hostname = urllib.parse.urlsplit(base_url).hostname
    return hostname in {"127.0.0.1", "localhost", "::1"}


def resource_collection_state(base_url: str, pid: Optional[int]) -> tuple[bool, str]:
    if pid is None:
        return False, "未采集（未提供 --server-pid）"
    if not is_local_base_url(base_url):
        return False, "未采集（base-url 不是本机地址）"
    if not Path(f"/proc/{pid}").is_dir():
        return False, f"未采集（/proc/{pid} 不存在）"
    try:
        read_process_sample(pid)
    except (OSError, ValueError) as exc:
        return False, f"未采集（无法读取 /proc/{pid}: {exc}）"
    return True, f"已采集（PID {pid}）"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Tinyweb 云盘正确性门禁与可重复性能压测")
    parser.add_argument("--base-url", default="http://127.0.0.1:1316")
    parser.add_argument("--username", default="benchmark_user")
    parser.add_argument("--password-env", default="TINYW_BENCHMARK_PASSWORD")
    parser.add_argument("--server-pid", type=int)
    parser.add_argument("--duration", type=int, default=30)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--static-connections", type=parse_positive_int_list, default=[10, 50, 100, 200])
    parser.add_argument("--api-connections", type=parse_positive_int_list, default=[10, 50, 100])
    parser.add_argument("--download-connections", type=parse_positive_int_list, default=[1, 5, 20])
    parser.add_argument("--upload-concurrency", type=parse_positive_int_list, default=[1, 2, 4])
    parser.add_argument("--file-size-mib", type=int, default=100)
    parser.add_argument("--auth-count", type=int, default=2000)
    parser.add_argument("--auth-workers", type=parse_positive_int_list, default=[10, 25, 50, 100])
    parser.add_argument("--skip-auth", action="store_true")
    parser.add_argument("--skip-upload", action="store_true")
    parser.add_argument("--skip-download", action="store_true")
    parser.add_argument("--smoke", action="store_true", help="缩短参数以验证流程；仍会发起真实请求")
    parser.add_argument("--output-dir", type=Path, default=Path("benchmark/results"))
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.duration <= 0 or args.runs <= 0 or args.threads <= 0 or args.auth_count <= 0:
        raise BenchmarkError("duration、runs、threads 和 auth-count 必须大于 0")
    if args.file_size_mib <= 0 or args.file_size_mib > 1024:
        raise BenchmarkError("file-size-mib 必须在 1 到 1024 之间")
    parsed = urllib.parse.urlsplit(args.base_url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise BenchmarkError("base-url 必须是有效的 http(s) URL")
    if parsed.username is not None or parsed.password is not None:
        raise BenchmarkError("base-url 不得包含用户名或密码")
    if parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise BenchmarkError("base-url 只能包含协议、主机和端口，不能包含路径、查询或片段")
    if args.server_pid is not None and args.server_pid <= 0:
        raise BenchmarkError("server-pid 必须大于 0")


def gate(gates: dict[str, dict[str, Any]], name: str, action: Any) -> bool:
    try:
        detail = action()
        gates[name] = {"status": "passed", "detail": detail or "通过"}
        return True
    except BenchmarkError as exc:
        gates[name] = {"status": "failed", "detail": str(exc)}
        return False


def mark_skipped(gates: dict[str, dict[str, Any]], name: str, reason: str) -> None:
    gates[name] = {"status": "skipped", "detail": reason}


def main() -> int:
    args = build_parser().parse_args()
    try:
        validate_args(args)
    except BenchmarkError as exc:
        print(f"参数错误: {exc}", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parents[1]
    try:
        validate_dependencies(repo_root, args.skip_auth)
    except BenchmarkError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    password = os.environ.get(args.password_env)
    if not password:
        print(f"环境变量 {args.password_env} 未设置；密码不会通过命令行参数接收。", file=sys.stderr)
        return 2

    if args.smoke:
        args.duration = 1
        args.runs = 1
        args.threads = min(args.threads, 2)
        args.static_connections = args.static_connections[:1]
        args.api_connections = args.api_connections[:1]
        args.download_connections = args.download_connections[:1]
        args.upload_concurrency = args.upload_concurrency[:1]
        args.file_size_mib = 1
        args.auth_count = min(args.auth_count, 10)
        args.auth_workers = args.auth_workers[:1]

    timestamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    output_root = args.output_dir
    if not output_root.is_absolute():
        output_root = repo_root / output_root
    run_dir = output_root / timestamp
    raw_dir = run_dir / "raw"
    raw_dir.mkdir(parents=True, exist_ok=False)

    collect_resources, resource_state = resource_collection_state(args.base_url, args.server_pid)
    environment = {
        "git_commit": git_commit(repo_root),
        "os": detect_os(),
        "kernel": platform.release(),
        "cpu_model": detect_cpu_model(),
        "cpu_cores": os.cpu_count() or 0,
        "memory_total_gib": detect_memory_gib(),
        "test_time": dt.datetime.now(dt.timezone.utc).astimezone().isoformat(),
        "base_url": args.base_url.rstrip("/"),
        "file_size_mib": args.file_size_mib,
        "duration_seconds": args.duration,
        "runs": args.runs,
        "threads": args.threads,
        "static_connections": args.static_connections,
        "api_connections": args.api_connections,
        "download_connections": args.download_connections,
        "upload_concurrency": args.upload_concurrency,
        "auth_count": args.auth_count,
        "auth_workers": args.auth_workers,
        "smoke": args.smoke,
        "wrk_version": command_version(["wrk", "--version"]),
        "curl_version": command_version(["curl", "--version"]),
        "resource_collection": resource_state,
    }
    write_json(run_dir / "environment.json", environment)

    gates: dict[str, dict[str, Any]] = {}
    rows: list[dict[str, Any]] = []
    errors: list[str] = []
    cleanup: dict[str, Any] = {"status": "not-run", "requested": 0, "deleted": 0, "failed": []}
    created_names: set[str] = set()
    warmup = 1 if args.smoke else 10

    with tempfile.TemporaryDirectory(prefix="tinyweb_benchmark_") as temporary:
        temp_dir = Path(temporary)
        cookie_jar = temp_dir / "cookies.txt"
        cookie_jar.touch()
        client = CurlClient(args.base_url, cookie_jar, temp_dir, timeout=max(120.0, args.duration + 30.0))
        session: Optional[str] = None
        test_file = temp_dir / "deterministic_test.bin"
        file_size = args.file_size_mib * 1024 * 1024
        source_hash = generate_test_file(test_file, file_size)
        gate_filename = f"benchmark_{timestamp}_gate.bin"
        encoded_gate_name = urllib.parse.quote(gate_filename, safe="")
        download_path = "/file/" + encoded_gate_name

        def check_index() -> str:
            response = client.request("GET", "/index.html")
            ensure_http_success(response, "GET /index.html", exact_status=200)
            return "HTTP 200"

        index_ok = gate(gates, "static_index", check_index)

        def authenticate() -> str:
            nonlocal session
            login = client.request(
                "POST",
                "/login.html",
                fields={"username": args.username, "password": password},
                update_cookies=True,
            )
            session = extract_session(cookie_jar)
            if session:
                return "既有用户登录成功并取得 session Cookie"
            unique_user = f"{args.username}_benchmark_{timestamp}"
            registration = client.request(
                "POST",
                "/register.html",
                fields={"username": unique_user, "password": password},
                update_cookies=True,
            )
            ensure_http_success(registration, "POST /register.html")
            session = extract_session(cookie_jar)
            if not session:
                second_login = client.request(
                    "POST",
                    "/login.html",
                    fields={"username": unique_user, "password": password},
                    update_cookies=True,
                )
                ensure_http_success(second_login, "POST /login.html")
                session = extract_session(cookie_jar)
            if not session:
                detail = login.stderr or f"首次登录 HTTP {login.status}"
                raise BenchmarkError(f"注册/登录后未取得 session Cookie（{detail}）")
            return "创建门禁用户并取得 session Cookie"

        auth_ok = gate(gates, "authentication", authenticate) if index_ok else False
        if not index_ok:
            mark_skipped(gates, "authentication", "静态入口门禁失败")

        def check_file_list() -> str:
            files = load_file_list(client)
            return f"合法 JSON，当前文件数 {len(files)}"

        file_list_ok = gate(gates, "file_list_json", check_file_list) if auth_ok else False
        if not auth_ok:
            mark_skipped(gates, "file_list_json", "认证门禁失败")

        needs_test_file = file_list_ok and (not args.skip_upload or not args.skip_download)

        def upload_gate_file() -> str:
            response = client.request(
                "POST", "/file", upload_path=test_file, upload_name=gate_filename
            )
            ensure_http_success(response, "门禁文件上传")
            created_names.add(gate_filename)
            files = load_file_list(client)
            if not any(item.get("file_name") == gate_filename for item in files):
                raise BenchmarkError("上传返回成功，但 GET /file 中未找到门禁文件")
            return f"上传并在文件列表中确认 {gate_filename}"

        upload_gate_ok = gate(gates, "upload", upload_gate_file) if needs_test_file else False
        if not needs_test_file:
            reason = "文件列表门禁失败" if not file_list_ok else "已同时跳过上传和下载场景"
            mark_skipped(gates, "upload", reason)

        def full_download_gate() -> str:
            target = temp_dir / "full_download.bin"
            response = client.request("GET", download_path, body_path=target)
            ensure_http_success(response, "完整下载", exact_status=200)
            if sha256_file(target) != source_hash:
                raise BenchmarkError("完整下载 SHA-256 与上传源文件不一致")
            return f"SHA-256 一致: {source_hash}"

        full_download_ok = False
        if upload_gate_ok and not args.skip_download:
            full_download_ok = gate(gates, "download_sha256", full_download_gate)
        else:
            reason = "上传门禁失败" if not upload_gate_ok else "已跳过下载场景"
            mark_skipped(gates, "download_sha256", reason)

        def range_gate() -> str:
            validate_range(client, download_path, test_file, file_size, "bytes=100-999", 100, 999)
            validate_range(client, download_path, test_file, file_size, "bytes=100-", 100, file_size - 1)
            validate_range(client, download_path, test_file, file_size, "bytes=-500", file_size - 500, file_size - 1)
            return "bytes=100-999、bytes=100-、bytes=-500 的状态码、响应头和正文均正确"

        range_ok = gate(gates, "range_download", range_gate) if full_download_ok else False
        if not full_download_ok:
            mark_skipped(gates, "range_download", "完整下载门禁未通过或已跳过")

        if index_ok:
            rows.extend(
                run_wrk_matrix(
                    test_name="static_index",
                    url=args.base_url.rstrip("/") + "/index.html",
                    connections=args.static_connections,
                    threads=args.threads,
                    duration=args.duration,
                    runs=args.runs,
                    warmup=warmup,
                    session=None,
                    temp_dir=temp_dir,
                    raw_dir=raw_dir,
                    server_pid=args.server_pid,
                    collect_resources=collect_resources,
                    errors=errors,
                )
            )

        if auth_ok and file_list_ok and session:
            rows.extend(
                run_wrk_matrix(
                    test_name="api_file_list",
                    url=args.base_url.rstrip("/") + "/file",
                    connections=args.api_connections,
                    threads=args.threads,
                    duration=args.duration,
                    runs=args.runs,
                    warmup=warmup,
                    session=session,
                    temp_dir=temp_dir,
                    raw_dir=raw_dir,
                    server_pid=args.server_pid,
                    collect_resources=collect_resources,
                    errors=errors,
                )
            )

        if auth_ok and not args.skip_auth:
            rows.extend(
                run_auth_benchmarks(
                    repo_root=repo_root,
                    base_url=args.base_url,
                    password=password,
                    count=args.auth_count,
                    workers=args.auth_workers,
                    timestamp=timestamp,
                    raw_dir=raw_dir,
                    server_pid=args.server_pid,
                    collect_resources=collect_resources,
                    errors=errors,
                )
            )

        if upload_gate_ok and not args.skip_upload:
            for concurrency in args.upload_concurrency:
                row, upload_names = run_upload_case(
                    client=client,
                    source_path=test_file,
                    concurrency=concurrency,
                    timestamp=timestamp,
                    raw_dir=raw_dir,
                    server_pid=args.server_pid,
                    collect_resources=collect_resources,
                )
                rows.append(row)
                created_names.update(upload_names)

        if range_ok and session and not args.skip_download:
            rows.extend(
                run_wrk_matrix(
                    test_name="download_file",
                    url=args.base_url.rstrip("/") + download_path,
                    connections=args.download_connections,
                    threads=args.threads,
                    duration=args.duration,
                    runs=args.runs,
                    warmup=warmup,
                    session=session,
                    temp_dir=temp_dir,
                    raw_dir=raw_dir,
                    server_pid=args.server_pid,
                    collect_resources=collect_resources,
                    errors=errors,
                )
            )

        if auth_ok:
            cleanup = cleanup_created_files(client, created_names)

    summary = {
        "environment_file": "environment.json",
        "gates": gates,
        "results": rows,
        "cleanup": cleanup,
        "errors": errors,
        "password_saved": False,
        "session_token_saved": False,
    }
    eligible, eligibility_reason = resume_eligibility(rows, gates, args)
    summary["resume_candidate"] = {"eligible": eligible, "reason": eligibility_reason}
    write_json(run_dir / "summary.json", summary)
    write_summary_csv(run_dir / "summary.csv", rows)
    write_report(run_dir / "report.md", environment, gates, rows, errors, cleanup, args)

    print(f"结果目录: {run_dir}")
    print(f"正确性门禁: {'通过' if all(item['status'] == 'passed' for item in gates.values()) else '未全部通过'}")
    print(f"简历候选: {'可用' if eligible else '不可用'}（{eligibility_reason}）")
    performance_ok = bool(rows) and all(
        int(row.get("failed_requests") or 0) == 0 and row.get("all_runs_zero_error", True)
        for row in rows
    )
    gates_ok = all(item["status"] in {"passed", "skipped"} for item in gates.values())
    return 0 if not errors and gates_ok and performance_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
