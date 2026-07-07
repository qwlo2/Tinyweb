#include <unistd.h>
#include "webserver.h"

int main() {
    /* 守护进程 后台运行 */
    //daemon(1, 0); 
    //线程池先关闭了
    WebServer server(
        1316, 1, 60000, 5,false,             /* 端口 ET模式 timeoutMs 优雅退出  */
        3306, "root", "123456", "tinywebserver", /* Mysql配置 */
        12,  0,false, 1, 1024);             /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.Start();
} 