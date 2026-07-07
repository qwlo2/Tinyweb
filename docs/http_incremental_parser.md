# HTTP 增量解析重构说明

## 目标

本次只改造 HTTP 请求解析与 `HttpConn` 集成，不改动 Reactor、线程池、数据库接口、页面资源和路由业务。新的解析器可以在请求分多次到达时保留未完成数据，只有完整请求到达后才进入路由处理。

## Parser 状态机

```text
REQUEST_LINE
  |  找到完整 CRLF 并解析成功
  v
HEADERS
  |  逐行消费 Header，遇到空行
  v
BODY
  |  ReadableBytes >= Content-Length
  v
FINISH
```

解析返回值由 `HttpRequest::ParseResult` 表示：

- `Complete`：一个完整请求已经解析完成。
- `Incomplete`：当前 Buffer 数据不足，不能构造响应。
- `BadRequest`：请求行、Header 或 `Content-Length` 格式错误，或收到暂不支持的 `Transfer-Encoding: chunked`。
- `PayloadTooLarge`：请求行、Header 总大小、Header 数量或 Body 超过限制。

集中限制定义在 `http/httprequest.h`：

- 请求行最大长度：8 KiB。
- Header 总大小：64 KiB。
- Header 数量：100。
- Body 最大长度：1 MiB。

## 解析流程

### REQUEST_LINE

解析器只有在 Buffer 中找到完整 `\r\n` 后才解析请求行。没有 CRLF 时返回 `Incomplete` 并保留 Buffer；超过请求行限制时返回 `PayloadTooLarge`。

### HEADERS

Header 每次只消费一条完整行。未找到完整 CRLF 时返回 `Incomplete`；遇到空行时 Header 结束。解析器会校验 `Content-Length` 是否为非负十进制整数，并拒绝重复 `Content-Length`。当前不支持 chunked，收到 `Transfer-Encoding: chunked` 时返回 `BadRequest`。

### BODY

如果存在 `Content-Length`，只有 `buffer.ReadableBytes() >= contentLength` 时才消费指定长度的 Body；否则返回 `Incomplete`。消费 Body 时只取属于当前请求的字节，Buffer 中属于下一次请求的剩余字节会继续保留。

## HttpConn 集成

`HttpConn::process()` 只在 `Incomplete` 时返回 `false`，上层 `WebServer::OnProcess()` 会重新注册 `EPOLLIN` 等待下一次读事件。

```text
Incomplete       -> 不生成响应，继续 EPOLLIN
Complete         -> 路由/静态文件响应，注册 EPOLLOUT
BadRequest       -> 生成 400，注册 EPOLLOUT
PayloadTooLarge  -> 生成 413，注册 EPOLLOUT
```

完成一个请求后会重置 `HttpRequest` 的解析状态，但不会清空已经留在 `readBuff_` 中的下一请求数据。错误请求会关闭 keep-alive 并清空当前读缓冲，避免继续解析不可信数据。

## Keep-Alive 与 Pipeline 边界

当前支持顺序 Keep-Alive：一个响应写完后，如果连接仍保持并且 Buffer 中已有下一请求，`OnWrite_()` 会再次调用 `OnProcess()` 继续处理。

当前未实现 HTTP pipeline 的并行响应调度；同一连接上的多个请求仍按“解析一个、响应一个、写完后再处理下一个”的顺序执行。

## 修改文件清单

- `http/httprequest.h`：新增 `ParseResult`、解析限制、Content-Length 状态字段和辅助函数声明。
- `src/httprequest.cpp`：实现增量状态机、Content-Length 校验、chunked 拒绝和大小限制。
- `http/httpconn.h`：保存当前响应的 keep-alive 状态，并按有效 iovec 统计待写字节。
- `src/httpconn.cpp`：按解析结果决定 EPOLLIN/EPOLLOUT，保留剩余 Buffer，并修正 ET 读到 0 时可能空转的问题。
- `src/httpresponse.cpp`：增加 413 状态码，并避免显式 400/413 被静态文件检查覆盖成 404。

## 验证清单

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过，无编译警告输出。
- 临时 parser 单元测试：通过，覆盖分片 GET、分片 POST、剩余请求保留、非法 `Content-Length`、chunked 拒绝、超长请求行。
- `ctest --test-dir build --output-on-failure`：项目当前没有注册测试用例。
- `curl` GET `/index.html`：返回 `HTTP/1.1 200 OK`。
- `curl` POST `/index.html`：返回 `HTTP/1.1 200 OK`。
- socket 分片 POST：先发送 Header 和部分 Body，补齐 Body 后返回 `HTTP/1.1 200 OK`。
- socket Keep-Alive 连续两个 GET：同一连接收到 2 个 HTTP 响应。
- 非法 `Content-Length: -1`：返回 `HTTP/1.1 400 Bad Request`。
- 超长请求行：返回 `HTTP/1.1 413 Payload Too Large`。

## 当前未支持的 HTTP 特性

- `Transfer-Encoding: chunked`。
- HTTP pipeline 的并行响应调度。
- multipart/form-data、JSON Body 的业务解析。
- 请求体流式处理。
