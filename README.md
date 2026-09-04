TinyCloudDrive

基于 C++20 / Linux / Epoll / Reactor 实现的多用户云盘服务器。项目在轻量级 WebServer 基础上扩展了用户认证、流式文件上传、内容去重、大文件下载、HTTP Range 断点续传、文件分享与 Redis 临时授权等能力。

分支：my-cloud-drive

功能特性

Main/Sub Reactor：主 EventLoop 接收连接，连接分发至多个 Sub Reactor 处理 I/O。

非阻塞网络 I/O：基于 Epoll，连接事件支持 ET/LT 与 EPOLLONESHOT。

用户认证：注册/登录密码使用 Argon2id 哈希验证，Session 由 Redis 管理。

流式上传：增量解析 multipart/form-data，处理 boundary 跨多次读取的情况，边接收边落盘。

内容去重：上传过程中增量计算 SHA-256，以内容哈希建立物理对象，并通过引用计数复用重复内容。

大文件下载：使用 Linux sendfile() 减少用户态数据拷贝，并通过 EPOLLOUT 处理非阻塞部分写。

HTTP Range：支持 bytes=start-end、bytes=start-、bytes=-suffix，返回 206 Partial Content；非法范围返回 416 Range Not Satisfiable。

文件分享：支持分享链接、提取码、有效期、分享列表、取消分享和分享下载。

临时分享授权：分享验证成功后在 Redis 中写入带 TTL 的授权 Token，下载前再次校验分享状态。

静态资源缓存：启动时预加载常用 HTML/CSS/JS 等静态资源。

技术栈

C++20 · Linux · Epoll · Reactor · HTTP/1.1 · MySQL · Redis · OpenSSL · Argon2id · CMake · pthread

架构

flowchart TD
    Client[Client / Browser] --> Main[Main EventLoop]
    Main --> Acceptor[Acceptor]
    Acceptor --> Pool[EventLoopPool]
    Pool --> Sub1[Sub Reactor]
    Pool --> Sub2[Sub Reactor]
    Pool --> SubN[Sub Reactor]

    Sub1 --> Http[HttpConn]
    Sub2 --> Http
    SubN --> Http

    Http --> Request[HTTP Incremental Parser]
    Request --> Auth[Auth / Session]
    Request --> Upload[Upload]
    Request --> Download[Download]
    Request --> Share[File Share]

    Auth --> Redis[(Redis)]
    Auth --> MySQL[(MySQL)]

    Upload --> SHA[SHA-256]
    Upload --> Object[Object Storage]
    Upload --> MySQL

    Download --> Object
    Download --> Sendfile[sendfile]
    Sendfile --> Client

    Share --> Redis
    Share --> MySQL
    Share --> Download

核心请求链路：

Client
  -> Main EventLoop / Acceptor
  -> Sub Reactor
  -> HttpConn
  -> HttpRequest
  -> Route
  -> Auth / Upload / Download / FileShare
  -> MySQL / Redis / Object Storage

核心设计

1. Reactor 网络模型

服务器采用 Main/Sub Reactor：

Main EventLoop 负责监听 Socket 与新连接接入；

新连接轮询分发至 EventLoopPool 中的 Sub Reactor；

Sub Reactor 负责客户端连接的 Epoll I/O；

文件、数据库等任务通过线程池执行，避免耗时业务长期阻塞事件循环。

2. multipart 流式上传

上传文件不整体加载到内存，而是持续消费 Socket 数据：

TCP
 -> Buffer
 -> multipart boundary 增量解析
 -> 文件数据写入临时文件
 -> EVP_DigestUpdate(SHA-256)
 -> 上传结束
 -> 生成最终内容哈希
 -> 建立/复用 object
 -> 建立用户逻辑文件映射

对于可能跨两次读取的 boundary，解析器保留未确认的尾部字节，只有确认属于文件正文的数据才写入磁盘。

3. SHA-256 内容寻址与去重

物理文件与用户逻辑文件分离：

User File
    |
    | object_id
    v
Object
    |- content_hash
    |- file_size
    |- storage_path
    `- ref_count

相同 SHA-256 的文件复用已有 Object，并增加引用计数；删除逻辑文件时相应减少引用，引用归零后删除物理对象。

4. sendfile 与 HTTP Range

下载通过 sendfile() 从文件描述符直接向 Socket 发送。

Range 支持：

Range: bytes=100-199
Range: bytes=100-
Range: bytes=-500

合法范围返回：

HTTP/1.1 206 Partial Content
Accept-Ranges: bytes
Content-Range: bytes 100-199/1000
Content-Length: 100

非法范围返回：

HTTP/1.1 416 Range Not Satisfiable
Content-Range: bytes */1000

下载过程中设置单次事件发送预算；Socket 发送缓冲区满时保存 offset，等待下一次 EPOLLOUT 后继续。

5. 分享授权

分享链路将“分享元数据”和“临时下载授权”分离：

创建分享
 -> share_token / 可选提取码 / expire_time
 -> 访问分享
 -> 校验有效期和提取码
 -> Redis 写入临时 share_auth
 -> 下载前再次验证 Redis + MySQL 分享状态
 -> 复用 Download 模块

Redis 临时分享授权当前 TTL 为 1800 秒。

项目结构

Tinyweb/
├── buffer/              # 网络缓冲区
├── http/                # HTTP、认证、上传、下载、分享接口声明
├── Pool/                # 线程池 / 连接池相关组件
├── server/              # Epoll、EventLoop、EventLoopPool、WebServer
├── src/                 # 主要实现
├── timer/               # 定时器
├── resources/           # HTML / CSS / JS 静态资源
├── data/
│   ├── tmp/             # 上传临时文件
│   └── object/          # 内容寻址物理对象
├── docs/                # 协议与实现说明
├── webbench-1.5/        # Web 压测工具
└── CMakeLists.txt

环境依赖

以 Ubuntu/Debian 为例：

sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    libargon2-dev \
    redis-server \
    mysql-server

项目通过 CMake 链接：

pthread
mysqlclient
argon2
hiredis
OpenSSL::Crypto

快速开始

1. 克隆分支

git clone -b my-cloud-drive --single-branch \
    https://github.com/qwlo2/Tinyweb.git

cd Tinyweb

2. 配置运行环境

启动 Redis：

sudo systemctl start redis-server
redis-cli -p 6379 PING

正常应返回：

PONG

MySQL 需要提前创建与代码匹配的数据库和数据表。当前业务代码使用的核心表包括：

user
file
object
share

当前仓库没有独立的 schema.sql 初始化脚本，因此首次运行前需要根据现有表结构准备数据库。

3. 修改本地配置

当前分支仍包含与开发机环境绑定的配置，运行前应检查：

src/main.cpp
src/webserver.cpp

重点修改：

WebServer 监听端口；

MySQL IP、端口、用户名、密码、数据库名；

Redis 地址/端口；

resources/ 静态资源目录。

当前 webserver.cpp 中静态资源路径使用了开发机绝对路径。克隆到其他机器后必须改为你自己的 resources 路径，否则静态页面无法正常加载。

4. Release 编译

性能测试建议使用 Release：

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j"$(nproc)"

生成：

build-release/Tinywebserver

5. 运行

建议从项目根目录启动：

./build-release/Tinywebserver

当前 main.cpp 的服务监听端口为 1316，若未修改配置，可通过：

http://<server-ip>:1316/

访问。

HTTP Range 示例

假设已经拥有某文件，可以使用 curl 验证 Range：

curl -v \
  -H "Range: bytes=100-199" \
  http://127.0.0.1:1316/file/<filename> \
  -o part.bin

也可以测试：

curl -H "Range: bytes=100-" ...
curl -H "Range: bytes=-500" ...

安全与可靠性设计

用户密码使用 Argon2id 加盐哈希，不保存明文密码；

SQL 查询中的关键业务路径使用 prepared statement；

上传临时文件采用受限权限创建；

SHA-256 用于内容寻址和重复文件识别；

Redis Session/分享授权设置过期时间；

文件对象使用引用计数维护逻辑文件与物理数据生命周期。

当前状态

该分支目前已经形成完整的云盘主链路：

注册 / 登录
   -> Session
   -> 文件列表
   -> 流式上传
   -> SHA-256 去重存储
   -> sendfile / Range 下载
   -> 分享创建 / 验证 / 下载 / 取消

当前仍建议继续完善工程化配置，例如将数据库、Redis、静态资源目录等硬编码参数迁移到配置文件或环境变量，并补充数据库初始化脚本与自动化测试。

License

本仓库未在当前分支根目录中提供明确的 License 文件；如计划公开发布或允许第三方复用，建议补充合适的开源许可证。
