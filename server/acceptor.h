#pragma once
#include <functional>
#include <memory>
#include <netinet/in.h>
//#include "common.h"
//class Sock;
//class Inetaddr;
class acceptor {
private:
 // DISALLOW_COPY_AND_MOVE(acceptor);
  //eventloop *loop;
  //int listenfd;
  //Inetaddr *sa;
  //Sock *so;
 // std::unique_ptr<channel> ch;
   int port;
  std::function<void(int,sockaddr_in)> newconnection;
public:
  //只有main的要主动启动listen
  acceptor(int port_);
  ~acceptor();
  //在server中回调newconnect函数
  void setnewconnectioncallback(const std::function<void(int,sockaddr_in)>& _cb);
  void acceptconnecting(int listenfd);
  //
  int create_socket();
  void bind(int fd,const char *ip, const int port);
  int creat_bind_sock(const char *ip, const int port);
  //专用于初始化listenfd
 int InitSocket_();

    int SetFdNonblock(int fd) ;
};