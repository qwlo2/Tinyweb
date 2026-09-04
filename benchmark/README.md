# Tinyweb 云盘压测

`run_benchmark.py` 使用 Python 标准库编排测试：功能门禁和并发上传调用系统 `curl`，静态页面、登录态文件列表和文件下载调用 `wrk --latency`。脚本不会修改服务器源码、接口或数据库结构，也不会把静态页面 QPS 描述成整个云盘业务 QPS。

## 依赖与准备

Ubuntu 安装依赖：

```bash
sudo apt install python3 curl wrk
```

先启动 Tinyweb，并准备一个测试账号的密码。密码只从指定环境变量读取，不写入结果；完整 Session Token 仅保存在运行期间的临时 Cookie 文件和临时 wrk Lua 文件中，退出后删除。

```bash
export TINYW_BENCHMARK_PASSWORD='请替换为测试密码'
```

若 `--username` 登录失败，门禁会注册一个带本轮时间戳的专用账号。登录注册压测也会创建带 `benchmark_时间戳_` 前缀的测试账号；项目没有删除用户接口，因此这些账号不会自动清理。

## 快速流程检查

`--smoke` 会把持续时间、连接组合、文件大小和认证数量缩小，但仍会对正在运行的服务发出真实请求：

```bash
python3 benchmark/run_benchmark.py \
  --smoke \
  --username benchmark_user \
  --password-env TINYW_BENCHMARK_PASSWORD \
  --server-pid "$(pgrep -n Tinywebserver)"
```

## 完整压测

下面的命令使用默认测试矩阵：静态连接数 `10,50,100,200`，文件列表连接数 `10,50,100`，下载连接数 `1,5,20`，上传并发 `1,2,4`，认证 workers `10,25,50,100`。每个 wrk 连接组合先预热 10 秒，再正式运行 30 秒并重复 3 次，汇总取中位数。

```bash
python3 benchmark/run_benchmark.py \
  --base-url http://127.0.0.1:1316 \
  --username benchmark_user \
  --password-env TINYW_BENCHMARK_PASSWORD \
  --server-pid "$(pgrep -n Tinywebserver)" \
  --duration 30 \
  --runs 3 \
  --threads 4 \
  --static-connections 10,50,100,200 \
  --api-connections 10,50,100 \
  --download-connections 1,5,20 \
  --upload-concurrency 1,2,4 \
  --file-size-mib 100 \
  --auth-count 2000 \
  --auth-workers 10,25,50,100 \
  --output-dir benchmark/results
```

`--file-size-mib` 支持 1 到 1024，即最大 1 GiB；默认不会生成 4 GiB 文件。若 base URL 不是本机地址、未传 `--server-pid`，或 PID 无效，CPU/RSS 会明确记录为“未采集”，不会写成 0。

`--threads` 是 wrk 的线程上限；当某组连接数小于该值时，脚本自动使用连接数作为线程数，避免例如单连接下载因线程数大于连接数而被 wrk 拒绝。CPU 百分比来自指定服务进程的 `/proc` 采样，可能因多核并行而高于 100%。

可按需使用 `--skip-auth`、`--skip-upload` 或 `--skip-download` 缩小范围。跳过业务场景或使用 smoke 模式时，报告不会生成可用于简历的候选描述。

## 正确性门禁

正式性能请求前，脚本按依赖关系验证：

1. `GET /index.html` 返回 200；
2. `POST /login.html`，必要时 `POST /register.html`，并取得 `session` Cookie；
3. `GET /file` 返回含 `files` 数组的合法 JSON；
4. 通过 multipart 字段 `file` 上传确定内容的测试文件；
5. 完整下载文件并比较 SHA-256；
6. 验证 `bytes=100-999`、`bytes=100-`、`bytes=-500` 的 206 状态、`Content-Range`、`Content-Length` 和正文内容。

门禁失败后，其依赖的性能场景不会继续执行。静态场景和不依赖该门禁的场景仍可产生独立、明确标记的结果。脚本结束时只按本轮内存中记录的精确文件名查找并删除 `benchmark_时间戳_` 测试文件；不会删除用户原有文件。清理失败会记录在报告中。

## 结果

每轮结果位于：

```text
benchmark/results/<时间戳>/
├── environment.json
├── summary.json
├── summary.csv
├── report.md
└── raw/
```

- `environment.json`：提交 SHA、操作系统、CPU、内存、参数和工具版本；
- `summary.json`：正确性门禁、结构化指标、清理状态和简历可用性；
- `summary.csv`：QPS、P50/P95/P99、错误数、吞吐量以及 CPU/RSS；
- `report.md`：分场景汇总和严格受门禁、错误率约束的简历候选描述；
- `raw/`：wrk 原始输出、并发上传明细和已脱敏的认证输出。

静态资源、文件列表 API、上传、下载、注册和登录始终是独立场景。只有全部正确性门禁通过、所有必要场景均执行、每次运行错误率为 0 且不是 smoke 模式时，报告才会标记候选描述可用于简历。

## 安全验证（不访问服务器）

```bash
python3 -m py_compile benchmark/run_benchmark.py
python3 -m unittest benchmark/tests/test_parser.py
python3 benchmark/run_benchmark.py --help
```
