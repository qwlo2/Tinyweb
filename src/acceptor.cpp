#include "acceptor.h"
#include "log.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
acceptor::acceptor(int port_):port(port_){
}
  acceptor::~acceptor(){

  }
  //在server中回调newconnect函数
  void acceptor::setnewconnectioncallback(const std::function<void(int,sockaddr_in)>& _cb){
         newconnection=_cb;
  }
  //分发给其他从reactor
  void acceptor::acceptconnecting(int listenfd){
      sockaddr_in addr;
      socklen_t addrLen=sizeof(addr);
        while (true) {
        int connFd = accept4(
            listenfd,
            reinterpret_cast<sockaddr*>(&addr),
            &addrLen,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );

        if (connFd >= 0) {
            SetFdNonblock(connFd);
            newconnection(connFd,addr);
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        break;
    }
  }
  int acceptor::SetFdNonblock(int fd) {
     return  fcntl(fd,F_SETFL,fcntl(fd, F_GETFL)|O_NONBLOCK);
}
  //
  int acceptor::create_socket(){
       return  socket(AF_INET,SOCK_STREAM,0);
        
  }
  void acceptor::bind(int fd,const char *ip, const int port){
     sockaddr_in sa;
      sa.sin_port=htons(port);
      sa.sin_family=AF_INET;
      //绑定所有ipv4地址
      //sa.sin_addr.s_addr=htonl(INADDR_ANY)
    inet_pton(AF_INET,ip,&sa.sin_addr);
     int ret=::bind(fd,(sockaddr*)&sa, sizeof(sa));
     if (ret<0) {
         close(fd);
          return ;
     }
  }
 int acceptor::creat_bind_sock(const char *ip, const int port){
    int clientfd=socket(AF_INET,SOCK_STREAM,0);
    if (clientfd<0) {
        LOG_ERROR("Create socket error!", port);
          return -1;
    }
    sockaddr_in sa;
    sa.sin_port=htons(port);
    sa.sin_family=AF_INET;
    inet_pton(AF_INET,ip,&sa.sin_addr);
     socklen_t len=sizeof(sa);
     int ret=::bind(clientfd,(sockaddr*)&sa,len);
     if (ret<0) {
        LOG_ERROR("listen bind error");
        close(clientfd);
        return  -1;
     }
     return clientfd;
 }
  int acceptor::InitSocket_(){
      int listenfd=socket(AF_INET,SOCK_STREAM,0);
      if (listenfd<0) {
         LOG_ERROR("Create socket error!", port);
          return false;
      }
      linger lin={0};
        // if(openLinger_>0){
           lin.l_linger=1;//延续时间/秒
           lin.l_onoff=1;
       // }
        int ret=setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&lin, sizeof(lin));
         if(ret < 0) {
        LOG_ERROR("Init linger error!",port);
        close(listenfd);
        return -1;
       }
       int optval=1;
       /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。reuseport才可以多个进程复用 */
       ret=setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval));
      if(ret==-1){
        LOG_ERROR("Init SO_REUSEADDR error!",port);
        close(listenfd);
        return -1;
      }
    sockaddr_in sa;
    sa.sin_port=htons(port);
    sa.sin_family=AF_INET;
   // inet_pton(AF_INET,"127.0.0.1",&sa.sin_addr);
   sa.sin_addr.s_addr=htonl(INADDR_ANY);
   socklen_t len=sizeof(sa);
     ret=::bind(listenfd,(sockaddr*)&sa,len);
     if (ret<0) {
        LOG_ERROR("listen bind error");
        close(listenfd);
        return  -1;
     }
     ret=::listen(listenfd,4096);
     if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port);
        close(listenfd);
        return false;
      }
      SetFdNonblock(listenfd);
       LOG_INFO("Server port:%d", port);
     return  listenfd;
  }