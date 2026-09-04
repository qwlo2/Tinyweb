import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from run_benchmark import (  # noqa: E402
    latency_to_ms,
    median_runs,
    parse_auth_output,
    parse_wrk_output,
    transfer_to_mib_s,
)


def wrk_output(
    *,
    average="500.00us",
    maximum="2.00ms",
    p50="450.00us",
    p95="1.50ms",
    p99="2.00ms",
    transfer="512.00KB",
    socket_line="",
    non_2xx_line="",
):
    return f"""Running 1s test @ http://127.0.0.1:1316/index.html
  4 threads and 10 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   {average}  100.00us   {maximum}   90.00%
    Req/Sec     250.00     10.00   300.00     80.00%
  Latency Distribution
     50%  450.00us
     75%  700.00us
     90%    1.00ms
     99%    2.00ms
  1000 requests in 1.00s, 512.00KB read
{socket_line}
{non_2xx_line}
Requests/sec:   1000.00
Transfer/sec:   {transfer}
TinyBench-P50: {p50}
TinyBench-P95: {p95}
TinyBench-P99: {p99}
"""


class UnitConversionTests(unittest.TestCase):
    def test_latency_units(self):
        self.assertAlmostEqual(latency_to_ms("125us"), 0.125)
        self.assertAlmostEqual(latency_to_ms("12.5ms"), 12.5)
        self.assertAlmostEqual(latency_to_ms("1.25s"), 1250.0)

    def test_transfer_units(self):
        self.assertAlmostEqual(transfer_to_mib_s("1024KB"), 1.0)
        self.assertAlmostEqual(transfer_to_mib_s("12.5MB"), 12.5)
        self.assertAlmostEqual(transfer_to_mib_s("1.5GB"), 1536.0)


class WrkParserTests(unittest.TestCase):
    def test_qps_and_percentiles(self):
        parsed = parse_wrk_output(wrk_output())
        self.assertEqual(parsed["total_requests"], 1000)
        self.assertAlmostEqual(parsed["qps"], 1000.0)
        self.assertAlmostEqual(parsed["latency_avg_ms"], 0.5)
        self.assertAlmostEqual(parsed["latency_p50_ms"], 0.45)
        self.assertAlmostEqual(parsed["latency_p95_ms"], 1.5)
        self.assertAlmostEqual(parsed["latency_p99_ms"], 2.0)
        self.assertAlmostEqual(parsed["transfer_mib_s"], 0.5)

    def test_ms_and_seconds_latency_from_wrk(self):
        parsed = parse_wrk_output(
            wrk_output(
                average="12.00ms",
                maximum="1.20s",
                p50="10.00ms",
                p95="750.00ms",
                p99="1.10s",
                transfer="2.00GB",
            )
        )
        self.assertAlmostEqual(parsed["latency_avg_ms"], 12.0)
        self.assertAlmostEqual(parsed["latency_max_ms"], 1200.0)
        self.assertAlmostEqual(parsed["latency_p99_ms"], 1100.0)
        self.assertAlmostEqual(parsed["transfer_mib_s"], 2048.0)

    def test_socket_and_non_2xx_errors(self):
        parsed = parse_wrk_output(
            wrk_output(
                socket_line="Socket errors: connect 1, read 2, write 3, timeout 4",
                non_2xx_line="Non-2xx or 3xx responses: 5",
            )
        )
        self.assertEqual(parsed["socket_errors"], 10)
        self.assertEqual(parsed["non_2xx_responses"], 5)
        self.assertEqual(parsed["failed_requests"], 15)
        self.assertEqual(parsed["successful_requests"], 985)


class MedianTests(unittest.TestCase):
    def make_row(self, qps, p95, total, failed=0):
        return {
            "test_name": "static_index",
            "concurrency": 10,
            "duration_seconds": 1.0,
            "total_requests": total,
            "successful_requests": total - failed,
            "failed_requests": failed,
            "qps": qps,
            "latency_avg_ms": p95 / 2,
            "latency_p50_ms": p95 / 3,
            "latency_p95_ms": p95,
            "latency_p99_ms": p95 * 1.2,
            "latency_max_ms": p95 * 1.5,
            "transfer_mib_s": qps / 100,
            "cpu_avg_percent": None,
            "cpu_peak_percent": None,
            "rss_avg_mib": None,
            "rss_peak_mib": None,
            "process_ok": True,
        }

    def test_multiple_runs_use_median(self):
        combined = median_runs(
            [
                self.make_row(100.0, 5.0, 100),
                self.make_row(900.0, 50.0, 900),
                self.make_row(400.0, 20.0, 400),
            ]
        )
        self.assertEqual(combined["total_requests"], 400)
        self.assertAlmostEqual(combined["qps"], 400.0)
        self.assertAlmostEqual(combined["latency_p95_ms"], 20.0)
        self.assertTrue(combined["all_runs_zero_error"])

    def test_any_failed_run_is_retained_for_eligibility(self):
        combined = median_runs(
            [
                self.make_row(100.0, 5.0, 100),
                self.make_row(400.0, 20.0, 400, failed=1),
                self.make_row(900.0, 50.0, 900),
            ]
        )
        self.assertFalse(combined["all_runs_zero_error"])


class AuthParserTests(unittest.TestCase):
    def test_register_and_login_summaries(self):
        output = """
===== 注册 汇总 =====
总请求数       : 20
成功数         : 20
失败数         : 0
阶段耗时       : 1.25 s
吞吐量         : 16.00 req/s
延迟(ms)       : avg=10.00, p50=9.00, p95=15.00, p99=18.00, max=20.00

===== 登录 汇总 =====
总请求数       : 20
成功数         : 19
失败数         : 1
阶段耗时       : 0.50 s
吞吐量         : 40.00 req/s
延迟(ms)       : avg=5.00, p50=4.00, p95=8.00, p99=9.00, max=10.00

===== 最终验证 =====
"""
        rows = parse_auth_output(output, concurrency=10)
        self.assertEqual([row["test_name"] for row in rows], ["auth_register", "auth_login"])
        self.assertEqual(rows[0]["successful_requests"], 20)
        self.assertEqual(rows[1]["failed_requests"], 1)
        self.assertAlmostEqual(rows[1]["latency_p99_ms"], 9.0)


if __name__ == "__main__":
    unittest.main()
