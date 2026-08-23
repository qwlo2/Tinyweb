#include "acceptor.h"
#include "download.h"
#include "epoll.h"
#include "eventloop.h"
#include "heaptimer.h"
#include "httpconn.h"
#include "httpresponse.h"
#include "log.h"
#include "threadpool.h"
#include "upload.h"

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
  {
    std::unique_lock<std::mutex> lock(mutex_);
    pendindtask.emplace_back(cb);
  }

      if (wakeupfd >= 0) {
        eventfd_t value = 1;
        eventfd_write(wakeupfd, value);
    }
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
bool eventloop::isCurrentConnection(
    int fd, const std::shared_ptr<HttpConn>& conn) const {
    if (!conn) {
        return false;
    }
   //在fd和连接复用时，通过user_count>1判断
   //这里通过持有的conn和httpcoon是否相同判断
    auto it = httpcoon.find(fd);
    return it != httpcoon.end() &&
           it->second == conn &&
           conn->GetFd() == fd;
}

void eventloop::closeconn(int fd){
     auto it = httpcoon.find(fd);
     if (it == httpcoon.end() || !it->second) {
         return;
     }
     ep->DleFd(fd);
     it->second->Close();
}
//处理登录和注册
void eventloop::handleAuth(int fd,const std::shared_ptr<HttpConn> conn){
               conn->Parseauth();
                 //先查后解析arg
                   ThreadPool::init_Db()->AddTask([this,fd,conn](){
                           if (conn->Auth_ar_and_sqlquary()) {
                                conn->makeResponse(responseResult::Auth);
                           }else {
                                 conn->makeResponse(responseResult::Unauthorized);
                           }
                           //conn->sta = ProcessResult::ReadyWrite;
                           push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLOUT | event); 
                                         });
                   });
              
}
 void eventloop::hadleUpload(int fd,const std::shared_ptr<HttpConn> conn){
       ThreadPool::init_File()->AddTask([fd,this,conn](){
                auto ret=std::move(conn->handle_upload_file());
                switch (ret) {
                    case Upload::NeedRead:
                                   ThreadPool::init_io()->AddTask([fd,this,conn](){
                                         push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLIN | event); 
                                         });
                                   });
                                   break;
                    case Upload::ReadyWrite:
                                       ThreadPool::init_io()->AddTask([fd,this,conn](){
                                         conn->makeResponse(responseResult::Upload);
                                         push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLOUT | event); 
                                         });
                                   });
                                   break;
                    case Upload::UploadError:
                                       ThreadPool::init_io()->AddTask([fd,this,conn](){
                                         conn->makeResponse(responseResult::ServerError);
                                         push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLOUT | event); 
                                         });
                                         LOG_DEBUG("download Error")
                                   });
                                   break;
                }
       });
 }
 void eventloop::handle_response_write(int fd,const std::shared_ptr<HttpConn> conn){
       auto ret=std::move(conn->handle_response_write());
        switch (ret) {
                    case DownloadResult::NeedWrite:
                                      //缓冲区满，响应报文还没进行文件传输
                                         push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLOUT | event); 
                                         });
                                   break;
                    case DownloadResult::Finished:
                                          //开始进行文件传输
                                          hadleDownload(fd, conn);
                                   break;
                    case DownloadResult::Error:
                                        //响应报文传输失败
                                        push_and_do_task([this, fd, conn] {
                                            if (isCurrentConnection(fd, conn)) {
                                                      closeconn(fd);
                                             }
                                          });
                }
 }
  void eventloop::hadleDownload(int fd,const std::shared_ptr<HttpConn> conn){
           ThreadPool::init_File()->AddTask([fd,this,conn](){
                auto ret=std::move(conn->handle_down());
                switch (ret) {
                    case DownloadResult::NeedWrite:
                                      //此时的作用是防止大文件长期占用线程，每256k进行轮换
                                   ThreadPool::init_io()->AddTask([fd,this,conn](){
                                         push_and_do_task([this, fd, conn]() {
                                               if (!isCurrentConnection(fd, conn)) {
                                                       return;
                                                 }
                                                 ep->ModFd(fd, EPOLLOUT | event); 
                                         });
                                   });
                                   break;
                    case DownloadResult::Finished:
                                          //当返回只为0，写完
                                       if (conn->IsKeepAlive()) {
                                          ThreadPool::init_io()->AddTask([this, fd,conn]() {
                                           //半包和刚好一个的情况已经处理，如果此时是黏包，应该直接进行解析
                                              auto sta= std::move(conn->process());
                                              if (sta==ProcessResult::NeedRead ) {
                                                 push_and_do_task([this, fd, conn]() {
                                                    if (!isCurrentConnection(fd, conn)) {
                                                          return;
                                                        }
                                                    ep->ModFd(fd,EPOLLIN|event);
                                                });
                     
                                             }else if (sta==ProcessResult::NeedAuth) {
                                                      handleAuth(fd, conn);
                                            }else if (sta==ProcessResult::Download) {
                                                      hadleDownload(fd, conn);
                                            }  else {                    
                                                    push_and_do_task([this, fd, conn]() {
                                                       if (!isCurrentConnection(fd, conn)) {
                                                                  return;
                                                       }
                                                         ep->ModFd(fd,EPOLLOUT|event);
                                                      });            
                    
                                                     }
                                                 });
                                           return ;
                                      }
                                   break;
                    case DownloadResult::Error:
                                        push_and_do_task([this, fd, conn] {
                                            if (isCurrentConnection(fd, conn)) {
                                                      closeconn(fd);
                                             }
                                          });
                }
       });
  }
 void eventloop::hadleShare(int fd,const std::shared_ptr<HttpConn> conn){
             ThreadPool::init_Db()->AddTask([this,fd,conn](){
                           //失败
                          if (!conn->handle_share()) {
                               conn->makeResponse(responseResult::ServerError);
                               push_and_do_task([this, fd, conn]() {
                                                       if (!isCurrentConnection(fd, conn)) {
                                                                  return;
                                                       }
                                                         ep->ModFd(fd,EPOLLOUT|event);
                                                      }); 
                             return 
                             ;
                          }
                          if (conn->sta==actual_ProcessResult::Download) {
                                 hadleDownload(fd,conn);
                                 return ;
                          }
                          //其他的返回
                          conn->makeResponse(conn->status_route(conn->sta));
                          push_and_do_task([this, fd, conn]() {
                                                       if (!isCurrentConnection(fd, conn)) {
                                                                  return;
                                                       }
                                                         ep->ModFd(fd,EPOLLOUT|event);
                                                      }); 
                   });
 }
void eventloop::DealRead(int fd){
     if (!httpcoon.contains(fd)) {
        return;
     }
     std::shared_ptr<HttpConn> conn = httpcoon[fd];
     if (!isCurrentConnection(fd, conn)) {
         return;
     }
     ExtentTime_(conn.get());
     Onread(fd, conn);
}
void eventloop::Onread(int fd,const std::shared_ptr<HttpConn> conn){
        //先不加入线程池
     //push_and_do_task(std::bind(&eventloop::Onread, this,fd));
    
     ThreadPool::init_io()->AddTask([this, fd, conn]() {
       int saveerrno = 0;
       int ret = conn->read(&saveerrno);
       if (ret < 0 && (saveerrno == EAGAIN || saveerrno == EWOULDBLOCK)) {
         // 当返回只为-1，即缓冲区为空，重新读
             push_and_do_task([this, fd, conn]() {
               if (!isCurrentConnection(fd, conn)) {
                 return;
               }
               ep->ModFd(fd, EPOLLIN | event); 
            });
        
      }else if (ret > 0) {
        //解析http报文
           auto sta= std::move(conn->process());
        //分别是incompete，needauth，compete
          if (sta==ProcessResult::NeedRead) {
              push_and_do_task([this, fd, conn]() {
                if (!isCurrentConnection(fd, conn)) {
                  return;
                }
                ep->ModFd(fd, EPOLLIN | event);
               });
              
          } else if (sta==ProcessResult::NeedAuth) {
              handleAuth(fd, conn);
          }else if (sta==ProcessResult::Upload) {
              hadleUpload(fd,conn);
          }else if (sta==ProcessResult::Download) {
              hadleDownload(fd,conn);
          }else if (sta==ProcessResult::share) {
              hadleShare(fd, conn);
          }else{
            push_and_do_task([this, fd, conn]() {
               if (!isCurrentConnection(fd, conn)) {
                 return;
               }
               ep->ModFd(fd, EPOLLOUT | event); 
          });
            return ;
       }
     }else {
      push_and_do_task([this, fd, conn] {
        if (isCurrentConnection(fd, conn)) {
          closeconn(fd);
        }
      });
     }
    });
}

void eventloop::DealWrite(int fd){
    if (!httpcoon.contains(fd )) {
        return;
    }
   std::shared_ptr<HttpConn>& conn = httpcoon[fd];
   if (!isCurrentConnection(fd, conn)) {
       return;
   }
   ExtentTime_(conn.get());
   Onwrite(fd, conn);
}
void eventloop::Onwrite(int fd,const std::shared_ptr<HttpConn> conn){
    //写完，未写完，没写三种情况
      ThreadPool::init_io()->AddTask([this,fd,conn](){
      // if (conn->sta==ProcessResult::NeedAuth ) {
      //     conn->makeResponse(HttpRequest::ParseResult::Complete);
      //       conn->sta = ProcessResult::ReadyWrite;
      // }
      if (conn->get_sta()==actual_ProcessResult::Download) {
             if (conn->get_download_inited()) {
                  handle_response_write(fd, conn);
             }else {
                hadleDownload(fd, conn);
             }
             return ;
        }
         int saveerrno=0;
         int ret=conn->write(&saveerrno);
     if (conn->ToWriteBytes()==0) {
            //当返回只为0，写完
            if (conn->IsKeepAlive()) {
              ThreadPool::init_io()->AddTask([this, fd,conn]() {
                //半包和刚好一个的情况已经处理，如果此时是黏包，应该直接进行解析
                 auto sta= std::move(conn->process());
                  if (sta==ProcessResult::NeedRead ) {
                          push_and_do_task([this, fd, conn]() {
                              if (!isCurrentConnection(fd, conn)) {
                                  return;
                              }
                              ep->ModFd(fd,EPOLLIN|event);
                          });
                     
                  }else if (sta==ProcessResult::NeedAuth) {
                      handleAuth(fd, conn);
                  } else {                    
                         push_and_do_task([this, fd, conn]() {
                              if (!isCurrentConnection(fd, conn)) {
                                  return;
                              }
                              ep->ModFd(fd,EPOLLOUT|event);
                          });            
                    
                  }
              });
              return ;
            }
    }else if (ret<0&&(saveerrno==EAGAIN||saveerrno==EWOULDBLOCK)) {
        //没写完
         push_and_do_task([this, fd, conn]() {
              if (!isCurrentConnection(fd, conn)) {
                  return;
              }
              ep->ModFd(fd,EPOLLOUT|event);
        });    
         return;
    } 
    //ret<0，出错
     push_and_do_task([this, fd, conn]() {
        if (isCurrentConnection(fd, conn)) {
          closeconn(fd);
        }
        });    
    });
}
// void eventloop::process(int fd){
//      //badqust,toolarge,compete都是true，要返回响应报文
//      //incompete才false
//       if (httpcoon[fd].process()) {
//           //写完只用才链接才保存
//           //这里不push，因为raad，write，都是pending里面的
//              ep->ModFd(fd,EPOLLOUT|event);
//              return;
//       }else {
//         // if (httpcoon[fd]->IsKeepAlive()) {
//         //请求体还没有解析，不知道keepalive

//         //onread后，读完分为读与未读，未读无条件epollin，读了则precess进行解析
//         //解析时，incompete则epollin，其他则write
//         //write分未写完，没写完，没写，写完判断是否未长连接，是则process，减少一次wait
//         //为写完则epollput，其他则close
//                  ep->ModFd(fd,EPOLLIN|event);
//                  return;
//         // }
//       }
//      // closeconn(fd);
// }
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
         std::shared_ptr<HttpConn> conn;

    auto it = httpcoon.find(fd);
    //无论时新fd，或者复用，或者旧连接被使用，都删除定时器
       timer->del_(fd);
    // map 自己持有一个引用；use_count == 1 说明没有异步任务持有它
    if (it != httpcoon.end() && it->second.use_count() == 1) {
        conn = it->second;
        conn->init(fd, addr);
    } else {
        conn = std::make_shared<HttpConn>();
        conn->init(fd, addr);
        httpcoon[fd] = conn;
        //fd第一次被分发或者旧连接被使用
    }
        if(HttpConn::userCount>MAX_FD){
                int ret=send(fd,"Server busy!",sizeof("Server busy!"),0);
               if(ret<0){
                LOG_WARN("error to client[%d] error!", fd);
              }
                httpcoon[fd]->Close();
                //SendError_(fd, "Server busy!");
                  LOG_WARN("Client is full");
                  return;
        }//所以临界的删去
        if (timeoutMS>0) {
            //timer_->add(fd,timeoutMS_,std::bind(&WebServer::CloseConn_,this,&httpcoon[fd]));
           std::weak_ptr<HttpConn> weakConn = conn;
           timer->add(fd,timeoutMS,[this,fd,weakConn](){
                auto conn = weakConn.lock();
                //安全保险加一，在timer->del_(fd)后，这个已经没事了
                if (conn && isCurrentConnection(fd, conn)) {
                    closeconn(fd);
                }
            });
        }
        SetFdNonblock(fd);
       if ( !ep->AddFd(fd,EPOLLIN|event)) {
             httpcoon[fd]->Close();
       }
        LOG_INFO("Client[%d] in",httpcoon[fd]->GetFd());
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
