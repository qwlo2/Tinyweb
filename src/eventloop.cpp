#include "acceptor.h"
#include "epoll.h"
#include "eventloop.h"
#include "heaptimer.h"
#include "httpconn.h"
#include "log.h"

#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>


bool eventloop::Is_in_own_thrad(){
   return  thread_id==std::this_thread::get_id();
}
eventloop::eventloop(int timems,int port_,int event):timeoutMS(timems),port(port_),event(event){
    //在创建时初始化
     thread_id=std::this_thread::get_id();
     ep=std::make_unique<Epoller>();
     ac=std::make_unique<acceptor>(port);
     timer=std::make_unique<HeapTimer>();
     
}
eventloop::eventloop(eventloop* loop){
    thread_id=loop->thread_id;
    ep=std::move(loop->ep);
    ac=std::move(loop->ac);
    timer=std::move(loop->timer);
    timeoutMS=loop->timeoutMS;
}
eventloop::~eventloop(){
  if (listenfd >= 0) {
    close(listenfd);
  }
  if (wakeupfd >= 0) {
    close(wakeupfd);
  }
}
void eventloop::loop(){
    int timeMs;
     while (!stopping) {
         timeMs=-1;
          if (timer) {
              timeMs=timer->GetNextTick();
          }
          auto event=  ep->Wait(timeMs);
         
          for (int i=0;i<event;++i) {
              int fd=ep->GetEventFd(i);
              int fd_event=ep->GetEvent(i);
              if (fd_event&(EPOLLERR|EPOLLRDHUP|EPOLLHUP)) {
                   push_and_do_task(std::bind(&eventloop::closeconn,this,fd));
              }else if (fd==wakeupfd) {
                 handlewakefd();
              }else if (fd_event&EPOLLIN) {
                 push_and_do_task(std::bind(&eventloop::DealRead,this,fd));
              }else if(fd_event&EPOLLOUT){
                 push_and_do_task(std::bind(&eventloop::DealWrite,this,fd));
              }

          }
           //将处理epoll响应和处理连接一起执行，swap后
           //丢连接和处理i/o错开
          Do_task();
          
     }
}
 void eventloop::Deallisten(){
       int timeMs=-1;
      // listenfd=ac->InitSocket_();
       while (!stopping) {
        //在main reactor 中只有listenfd
             ep->Wait(timeMs);
        //分发的函数在webserve中实现，哪里持有main和eventlooppool
        //用ac的回调绑定那个即可,且在哪里加入httpconn的map
            ac->acceptconnecting(listenfd);
            
       }
 }


void eventloop::push_and_do_task(const std::function<void()> &cb){
     if (Is_in_own_thrad()) {
          cb();
     }else {
        pushtask(cb);
     }
}
void eventloop::pushtask(const std::function<void()>& cb){
    //好像回退化成拷贝
    //这个
    std::unique_lock<std::mutex> lock(mutex_);
     pendindtask.emplace_back(cb);
}
void eventloop::Do_task(){
    std::vector<std::function<void()>> func{};
     {
         std::unique_lock<std::mutex> lock(mutex_);
         //直接swap，push和do就可以异步
         func.swap(pendindtask);
     }
     for (auto& it:func) {
         it();
     }
}

//
void eventloop::closeconn(int fd){
      ep->DleFd(fd);
     if (httpcoon.find(fd)!=httpcoon.end()) {
            httpcoon[fd].Close();
     }
     
}
void eventloop::DealRead(int fd){
     ExtentTime_(&httpcoon[fd]);
     //先不加入线程池
     push_and_do_task(std::bind(&eventloop::Onread, this,fd));
}
void eventloop::Onread(int fd){
    int saveerrno=0;
    int ret=httpcoon[fd].read(&saveerrno);
    if (ret<0&&(saveerrno==EAGAIN||saveerrno==EWOULDBLOCK)) {
            //当返回只为-1，即第一次就是-1，重新读
            // if (httpcoon[fd]->IsKeepAlive()) {
               ep->ModFd(fd,EPOLLIN|event);
               return;
            //}
    }else if (ret!=0) {
    //读完解析
       process(fd);
       return;
    }
    //读完==0且不是长连接
  closeconn(fd);
}

void eventloop::DealWrite(int fd){
   ExtentTime_(&httpcoon[fd]);
   push_and_do_task(std::bind(&eventloop::Onwrite, this,fd));
}
void eventloop::Onwrite(int fd){
    int saveerrno=0;
    //写完，未写完，没写三种情况
    //
    int ret=httpcoon[fd].write(&saveerrno);
     if (httpcoon[fd].ToWriteBytes()==0) {
            //当返回只为0，写完
            if (httpcoon[fd].IsKeepAlive()) {
              process(fd);
              return;
            }
    }else if (ret<0&&(saveerrno==EAGAIN||saveerrno==EWOULDBLOCK)) {
        //没写完
         ep->ModFd(fd,EPOLLOUT|event);
         return;
    } 
    //ret<0，出错
  closeconn(fd);
}
void eventloop::process(int fd){
     //badqust,toolarge,compete都是true，要返回响应报文
     //incompete才false
      if (httpcoon[fd].process()) {
          //写完只用才链接才保存
          //这里不push，因为raad，write，都是pending里面的
             ep->ModFd(fd,EPOLLOUT|event);
             return;
      }else {
        // if (httpcoon[fd]->IsKeepAlive()) {
        //请求体还没有解析，不知道keepalive

        //onread后，读完分为读与未读，未读无条件epollin，读了则precess进行解析
        //解析时，incompete则epollin，其他则write
        //write分未写完，没写完，没写，写完判断是否未长连接，是则process，减少一次wait
        //为写完则epollput，其他则close
                 ep->ModFd(fd,EPOLLIN|event);
                 return;
        // }
      }
      closeconn(fd);
}
int eventloop::SetFdNonblock(int fd) {
     return  fcntl(fd,F_SETFL,fcntl(fd, F_GETFL)|O_NONBLOCK);
}
void eventloop::SendError_(int fd, const char *info) {
    int ret=send(fd,info,sizeof(info),0);
        if(ret<0){
                LOG_WARN("error to client[%d] error!", fd);
        }
       close(fd);
}
void eventloop::ExtentTime_(HttpConn *client) {
       if(timeoutMS>0){
        timer->adjust(client->GetFd(),timeoutMS);
        }
}
bool eventloop::initsock(){
    listenfd=ac->InitSocket_();
    if (listenfd<0) {
       return false;
    }
   int ret= ep->AddFd(listenfd,event|EPOLLIN);
   if(!ret){
        LOG_ERROR("Epoll add error!", port);
        close(listenfd);
        return false;
      }
    return true;
}
 void eventloop::AddClient_(int fd, sockaddr_in addr){
         assert(fd>0);
        // httpcoon[fd]=std::make_shared<HttpConn>();
        //改为httpconn后应该连接只需要清理资源，但不用重复创建资源
        httpcoon[fd].init(fd,addr);//fd复用
        if(HttpConn::userCount>MAX_FD){
                int ret=send(fd,"Server busy!",sizeof("Server busy!"),0);
               if(ret<0){
                LOG_WARN("error to client[%d] error!", fd);
              }
                httpcoon[fd].Close();
                //SendError_(fd, "Server busy!");
                  LOG_WARN("Client is full");
                  return;
        }//所以临界的删去
        if (timeoutMS>0) {
            //timer_->add(fd,timeoutMS_,std::bind(&WebServer::CloseConn_,this,&httpcoon[fd]));
           timer->add(fd,timeoutMS,[this,fd](){
                closeconn(fd);
            });
        }
        SetFdNonblock(fd);
       if ( !ep->AddFd(fd,EPOLLIN|event)) {
             httpcoon[fd].Close();
       }
        LOG_INFO("Client[%d] in",httpcoon[fd].GetFd());
 }
 void eventloop::setdealconn(const std::function<void(int,sockaddr_in)>& cb){
    ac->setnewconnectioncallback(cb);
 }
void eventloop::setwakefd(){
         //第一个是初始值
         int fd=eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC);
         if (fd<0) {
            LOG_ERROR("wakefd create error",port);
         }else {
           wakeupfd=fd;
           uint32_t wakeEvent = EPOLLIN;
           if (event & EPOLLET) {
             wakeEvent |= EPOLLET;
           }
           ep->AddFd(wakeupfd,wakeEvent);
         }

      }
 int eventloop::getwakefd(){
    return  wakeupfd;
 }
bool eventloop::handlewakefd(){
      uint64_t one=0;
     do{ 
         int len=read(wakeupfd, &one,sizeof(one));
        if (len<0&&(errno==EAGAIN||errno==EWOULDBLOCK)) {
             return true;
        }
     }while(EPOLLET&event);
     //专门的唤醒fd
    //  eventfd_t value=1;
    //  eventfd_read(wakeupfd,&value);
    return true;
}