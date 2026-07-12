#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
批量注册并登录验证脚本（仅使用 Python 标准库）

流程：
1. 生成 count 个唯一用户名；
2. 并发调用 /register 注册；
3. 等待短暂间隔；
4. 使用相同账号密码并发调用 /login；
5. 统计登录成功数、失败数、超时数、QPS 和延迟分位数；
6. 将失败详情写入 CSV。

注意：
很多教学 WebServer 即使业务失败也返回 HTTP 200，因此本脚本会检查响应正文中的
“失败标记”。如成功页面有固定文字，可通过 --login-success-marker 开启严格判定。
"""

from __future__ import annotations

import argparse
import csv
import http.client
import math
import socket
import statistics
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional
from urllib.parse import urlencode, urlsplit


@dataclass
class Result:
    stage: str
    username: str
    success: bool
    status: int
    latency_ms: float
    error_type: str = ""
    detail: str = ""
    body_snippet: str = ""


class HttpTarget:
    def __init__(self, base_url: str, timeout: float) -> None:
        parsed = urlsplit(base_url)
        if parsed.scheme not in ("http", "https"):
            raise ValueError("base-url 必须以 http:// 或 https:// 开头")

        self.scheme = parsed.scheme
        self.host = parsed.hostname or "127.0.0.1"
        self.port = parsed.port or (443 if self.scheme == "https" else 80)
        self.base_path = parsed.path.rstrip("/")
        self.timeout = timeout

    def post_form(self, path: str, form: dict[str, str]) -> tuple[int, str]:
        full_path = f"{self.base_path}/{path.lstrip('/')}"
        body = urlencode(form).encode("utf-8")

        headers = {
            "Content-Type": "application/x-www-form-urlencoded",
            "Content-Length": str(len(body)),
            "Connection": "close",
            "User-Agent": "TinyWeb-Register-Login-Test/1.0",
        }

        connection_cls = (
            http.client.HTTPSConnection
            if self.scheme == "https"
            else http.client.HTTPConnection
        )

        conn = connection_cls(self.host, self.port, timeout=self.timeout)
        try:
            conn.request("POST", full_path, body=body, headers=headers)
            response = conn.getresponse()
            response_body = response.read()
            return response.status, response_body.decode("utf-8", errors="replace")
        finally:
            conn.close()


def parse_markers(raw: str) -> List[str]:
    return [item.strip() for item in raw.split(",") if item.strip()]


def contains_any(text: str, markers: Iterable[str]) -> Optional[str]:
    folded = text.casefold()
    for marker in markers:
        if marker.casefold() in folded:
            return marker
    return None


def classify(
    status: int,
    body: str,
    failure_markers: List[str],
    success_marker: str,
) -> tuple[bool, str]:
    if not 200 <= status < 400:
        return False, f"HTTP status={status}"

    failure_marker = contains_any(body, failure_markers)
    if failure_marker is not None:
        return False, f"命中失败标记: {failure_marker}"

    if success_marker and success_marker.casefold() not in body.casefold():
        return False, f"未命中成功标记: {success_marker}"

    return True, ""


def request_once(
    target: HttpTarget,
    stage: str,
    path: str,
    username: str,
    password: str,
    username_field: str,
    password_field: str,
    failure_markers: List[str],
    success_marker: str,
) -> Result:
    started = time.perf_counter()
    try:
        status, body = target.post_form(
            path,
            {
                username_field: username,
                password_field: password,
            },
        )
        latency_ms = (time.perf_counter() - started) * 1000.0
        success, detail = classify(
            status=status,
            body=body,
            failure_markers=failure_markers,
            success_marker=success_marker,
        )
        return Result(
            stage=stage,
            username=username,
            success=success,
            status=status,
            latency_ms=latency_ms,
            error_type="" if success else "business_failure",
            detail=detail,
            body_snippet="" if success else " ".join(body[:300].split()),
        )
    except (socket.timeout, TimeoutError) as exc:
        return Result(
            stage=stage,
            username=username,
            success=False,
            status=0,
            latency_ms=(time.perf_counter() - started) * 1000.0,
            error_type="timeout",
            detail=str(exc) or "request timeout",
        )
    except (ConnectionError, OSError, http.client.HTTPException) as exc:
        return Result(
            stage=stage,
            username=username,
            success=False,
            status=0,
            latency_ms=(time.perf_counter() - started) * 1000.0,
            error_type="transport_error",
            detail=repr(exc),
        )
    except Exception as exc:
        return Result(
            stage=stage,
            username=username,
            success=False,
            status=0,
            latency_ms=(time.perf_counter() - started) * 1000.0,
            error_type="unexpected_error",
            detail=repr(exc),
        )


def percentile(sorted_values: List[float], p: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]

    rank = (len(sorted_values) - 1) * p
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return sorted_values[low]

    weight = rank - low
    return sorted_values[low] * (1.0 - weight) + sorted_values[high] * weight


def print_summary(stage: str, results: List[Result], elapsed: float) -> None:
    total = len(results)
    successes = sum(item.success for item in results)
    failures = total - successes
    timeouts = sum(item.error_type == "timeout" for item in results)
    transport_errors = sum(item.error_type == "transport_error" for item in results)
    business_failures = sum(item.error_type == "business_failure" for item in results)

    latencies = sorted(item.latency_ms for item in results)
    avg = statistics.fmean(latencies) if latencies else 0.0

    print(f"\n===== {stage} 汇总 =====")
    print(f"总请求数       : {total}")
    print(f"成功数         : {successes}")
    print(f"失败数         : {failures}")
    print(f"业务失败       : {business_failures}")
    print(f"超时数         : {timeouts}")
    print(f"网络/连接错误  : {transport_errors}")
    print(f"成功率         : {(successes / total * 100.0) if total else 0.0:.2f}%")
    print(f"阶段耗时       : {elapsed:.2f} s")
    print(f"吞吐量         : {(total / elapsed) if elapsed > 0 else 0.0:.2f} req/s")
    print(
        "延迟(ms)       : "
        f"avg={avg:.2f}, "
        f"p50={percentile(latencies, 0.50):.2f}, "
        f"p95={percentile(latencies, 0.95):.2f}, "
        f"p99={percentile(latencies, 0.99):.2f}, "
        f"max={(latencies[-1] if latencies else 0.0):.2f}"
    )


def run_phase(
    *,
    target: HttpTarget,
    stage: str,
    path: str,
    users: List[str],
    password: str,
    workers: int,
    username_field: str,
    password_field: str,
    failure_markers: List[str],
    success_marker: str,
) -> tuple[List[Result], float]:
    started = time.perf_counter()
    results: List[Result] = []
    completed = 0
    progress_lock = threading.Lock()

    print(f"\n开始 {stage}：{len(users)} 个用户，并发线程={workers}")

    with ThreadPoolExecutor(max_workers=workers, thread_name_prefix=stage) as executor:
        futures = [
            executor.submit(
                request_once,
                target,
                stage,
                path,
                username,
                password,
                username_field,
                password_field,
                failure_markers,
                success_marker,
            )
            for username in users
        ]

        for future in as_completed(futures):
            result = future.result()
            results.append(result)

            with progress_lock:
                completed += 1
                if completed % 100 == 0 or completed == len(users):
                    success_count = sum(item.success for item in results)
                    print(
                        f"\r{stage} 进度: {completed}/{len(users)}, "
                        f"当前成功: {success_count}",
                        end="",
                        flush=True,
                    )

    print()
    return results, time.perf_counter() - started


def write_failures(path: Path, results: List[Result]) -> None:
    failed = [item for item in results if not item.success]
    with path.open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "stage",
                "username",
                "status",
                "latency_ms",
                "error_type",
                "detail",
                "body_snippet",
            ]
        )
        for item in failed:
            writer.writerow(
                [
                    item.stage,
                    item.username,
                    item.status,
                    f"{item.latency_ms:.3f}",
                    item.error_type,
                    item.detail,
                    item.body_snippet,
                ]
            )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="批量注册 2000 个用户并通过登录成功数验证写入结果"
    )
    parser.add_argument("--base-url", default="http://127.0.0.1:1316")
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--workers", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--pause", type=float, default=1.0)
    parser.add_argument("--register-path", default="/register")
    parser.add_argument("--login-path", default="/login")
    parser.add_argument("--username-field", default="username")
    parser.add_argument("--password-field", default="password")
    parser.add_argument("--password", default="Test@123456")
    parser.add_argument(
        "--prefix",
        default="batch_user",
        help="用户名格式为 prefix_时间戳_序号，避免重复执行时冲突",
    )
    parser.add_argument(
        "--register-failure-markers",
        default="错误,操作没有成功,注册失败,用户已存在,already exists",
        help="逗号分隔；响应正文命中任意一个即判定注册失败",
    )
    parser.add_argument(
        "--login-failure-markers",
        default="错误,操作没有成功,登录失败,重新登录,invalid,password incorrect",
        help="逗号分隔；响应正文命中任意一个即判定登录失败",
    )
    parser.add_argument(
        "--register-success-marker",
        default="",
        help="可选；设置后，注册响应正文必须包含该字符串才算成功",
    )
    parser.add_argument(
        "--login-success-marker",
        default="",
        help="可选；例如成功页固定包含“欢迎”时可设置为 欢迎",
    )
    parser.add_argument(
        "--failures-csv",
        default="reg_login_failures.csv",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()

    if args.count <= 0:
        print("--count 必须大于 0", file=sys.stderr)
        return 2
    if args.workers <= 0:
        print("--workers 必须大于 0", file=sys.stderr)
        return 2

    timestamp = time.strftime("%Y%m%d_%H%M%S")
    users = [
        f"{args.prefix}_{timestamp}_{index:04d}"
        for index in range(1, args.count + 1)
    ]

    target = HttpTarget(args.base_url, args.timeout)

    print("测试配置：")
    print(f"Base URL       : {args.base_url}")
    print(f"用户数         : {args.count}")
    print(f"并发线程       : {args.workers}")
    print(f"单请求超时     : {args.timeout}s")
    print(f"用户名示例     : {users[0]} ... {users[-1]}")
    print(f"统一密码       : {args.password}")
    print("提示：用户名带时间戳，因此重复执行不会与旧用户冲突。")

    register_results, register_elapsed = run_phase(
        target=target,
        stage="register",
        path=args.register_path,
        users=users,
        password=args.password,
        workers=args.workers,
        username_field=args.username_field,
        password_field=args.password_field,
        failure_markers=parse_markers(args.register_failure_markers),
        success_marker=args.register_success_marker,
    )
    print_summary("注册", register_results, register_elapsed)

    if args.pause > 0:
        print(f"\n等待 {args.pause:.1f}s 后开始登录验证……")
        time.sleep(args.pause)

    login_results, login_elapsed = run_phase(
        target=target,
        stage="login",
        path=args.login_path,
        users=users,
        password=args.password,
        workers=args.workers,
        username_field=args.username_field,
        password_field=args.password_field,
        failure_markers=parse_markers(args.login_failure_markers),
        success_marker=args.login_success_marker,
    )
    print_summary("登录", login_results, login_elapsed)

    all_results = register_results + login_results
    failure_path = Path(args.failures_csv)
    write_failures(failure_path, all_results)

    register_successes = sum(item.success for item in register_results)
    login_successes = sum(item.success for item in login_results)

    print("\n===== 最终验证 =====")
    print(f"尝试注册用户数 : {args.count}")
    print(f"注册响应成功数 : {register_successes}")
    print(f"登录成功数     : {login_successes}")
    print(f"登录失败数     : {args.count - login_successes}")
    print(f"失败详情       : {failure_path.resolve()}")

    if login_successes == args.count:
        print("结论：2000 个用户全部能够登录，注册写入与登录读取链路验证通过。")
        return 0

    print(
        "结论：存在用户无法登录。请查看 CSV 中的失败类型和响应片段，"
        "确认是超时、注册失败、LSM 写入失败还是登录校验失败。"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
