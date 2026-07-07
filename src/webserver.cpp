#include "webserver.h"
#include "eventLoopPool.h"
#include "eventloop.h"
#include "log.h"
#include "sqlconnpool.h"
#include <arpa/inet.h>

#include <functional>
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
WebServer::WebServer(
        int port, int trigMode, int timeoutMS, int nums,bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize):port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(std::make_unique<HeapTimer> ()), threadpool_(std::make_unique<ThreadPool>(threadNum)), epoller_(std::make_unique<Epoller>()){
          // 使用绝对路径，避免工作目录的影响
          srcDir_ = (char*)malloc(256);
          strcpy(srcDir_, "/home/qiu/Tinyweberever/resources");
          HttpConn::userCount=0;
          HttpConn::srcDir=srcDir_;
          if(openLog) {
             Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
          }
          //Instance()->Init和InitSocket_()都用了log，因此要先
          SqlConnPool::Instance()->Init("192.168.1.6",sqlPort,sqlUser,sqlPwd,dbName,connPoolNum);
          InitEventMode_(trigMode);
          //主从reactor
          loop=std::make_unique<eventloop>(timeoutMS,port,listenEvent_);
          //ac是私有变量，通过这个函数才可以设置
          loop->setdealconn(std::bind(&WebServer::dealconn,this, std::placeholders::_1,std::placeholders::_2));

          eventpool=std::make_unique<eventLoopPool>(nums,timeoutMS,port,connEvent_);
         
           if(!loop->initsock()) { 
                isClose_ = true;
           }

        if(openLog){
       // Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
           if(isClose_) { 
                LOG_ERROR("========== Server init error!=========="); 
           }
           else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
          }
        }
  //linger是残留的意思，当tcp关闭，open这个，会在一段时间后再关闭（发送数据）
  //2个数据结果，l_onoff，l_linger超时时间（秒）
}
 WebServer::~WebServer(){
        isClose_=true;
        //close(listenFd_);
        loop->stop();
        eventpool->stop();

        free(srcDir_);
        SqlConnPool::Instance()->ClosePool();
 }
 void WebServer::Start(){
        eventpool->startloopPool();
        loop->Deallisten();
 }
// void WebServer::Start(){
//          int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
//         if(!isClose_) { 
//          LOG_INFO("========== Server start =========="); 
//         }
//         while(!isClose_){
//             if(timeoutMS_>0){
//                 //每隔一个tick time 处理epoll的有响应连接和过期连接
//                timeMS=timer_->GetNextTick();
//            }
//         int nums=epoller_->Wait(timeMS);
//         for(int i=0;i<nums;i++){
//             int fd=epoller_->GetEventFd(i);
//             uint32_t event_=epoller_->GetEvent(i);
//             if(fd==listenFd_){
//                 DealListen_();
//             }
//             else if (event_&EPOLLIN) {
//                    assert(users_.count(fd)>0);
//                    DealRead_(&users_[fd]);
//             }
//             else if(event_&EPOLLOUT){
//                  assert(users_.count(fd)>0);
//                  DealWrite_(&users_[fd]);
//             }
//             else if (event_&(EPOLLERR|EPOLLRDHUP|EPOLLHUP)) {
//                  assert(users_.count(fd) > 0);
//                  CloseConn_(&users_[fd]);
//             }
//             else {
//                   LOG_ERROR("Unexpected event");
//             }
//         }
//     }
// }
// bool WebServer::InitSocket_(){
//         sockaddr_in addr;
//         addr.sin_family=AF_INET;
//         addr.sin_port=htons(port_);
//         addr.sin_addr.s_addr=htonl(INADDR_ANY);
//         if(port_>65535||port_<1024){//port是16位整数<65535,1023为熟知port
//            LOG_ERROR("Port:%d error!",  port_);
//             return false;
//         }
//         listenFd_=socket(AF_INET,SOCK_STREAM,0);
//         if (listenFd_<0) {
//             LOG_ERROR("Create socket error!", port_);
//             return false;
//         } 
//         linger lin={0};
//         if(openLinger_>0){
//            lin.l_linger=1;//延续时间/秒
//            lin.l_onoff=1;
//         }
//         int ret=setsockopt(listenFd_,SOL_SOCKET,SO_LINGER,&lin, sizeof(lin));
//         if(ret < 0) {
//         LOG_ERROR("Init linger error!", port_);
//         close(listenFd_);
//         return false;
//        }
//        int optval=1;
//        /* 端口复用 */
//     /* 只有最后一个套接字会正常接收数据。reuseport才可以多个进程复用 */
//        ret=setsockopt(listenFd_,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval));
//       if(ret==-1){
//         LOG_ERROR("Init SO_REUSEADDR error!", port_);
//         close(listenFd_);
//         return false;
//       }
//       ret=bind(listenFd_,(sockaddr*)&addr,sizeof(addr));
//       if(ret<0){
//         LOG_ERROR("Bind listrnfd error!", port_);
//         close(listenFd_);
//         return false;
//       }
//        ret = listen(listenFd_, 1024);
//        if(ret < 0) {
//         LOG_ERROR("Listen port:%d error!", port_);
//         close(listenFd_);
//         return false;
//       }
//       ret=epoller_->AddFd(listenFd_,listenEvent_|EPOLLIN);
//       if(!ret){
//         LOG_ERROR("Epoll add error!", port_);
//         close(listenFd_);
//         return false;
//       }
//     SetFdNonblock(listenFd_);
//     LOG_INFO("Server port:%d", port_);
//     return true;
        
// }
void WebServer::InitEventMode_(int trigMode){
      connEvent_=EPOLLONESHOT|EPOLLRDHUP;
      listenEvent_=EPOLLRDHUP;
      switch (trigMode) {
          case 0:
                break;
          case 1:
               connEvent_|=EPOLLET;
                break;
          case 2:
               listenEvent_|=EPOLLET;
                 break;
          case 3:
                connEvent_|=EPOLLET;
                listenEvent_|=EPOLLET;
                break;
          default:
                 connEvent_|=EPOLLET;
                listenEvent_|=EPOLLET;
                break;
      }
      HttpConn::isET=(connEvent_&EPOLLET);
}
void WebServer::dealconn(int fd,sockaddr_in sa){
    auto& subractor=eventpool->nextloop();
    subractor->pushtask([&subractor,fd,sa](){
            subractor->addclient(fd, sa);
    });
   
//     uint64_t one=1;
//     write(subractor->getwakefd(),&one,sizeof(one));
   eventfd_t value = 1;
    ::eventfd_write(subractor->getwakefd(), value);
}
// void WebServer::AddClient_(int fd, sockaddr_in addr){//epoll，user的http，timer，log
//         assert(fd>0);
//         users_[fd].init(fd,addr);//fd复用
//         if(HttpConn::userCount>MAX_FD){
//                 int ret=send(fd,"Server busy!",sizeof("Server busy!"),0);
//                if(ret<0){
//                 LOG_WARN("error to client[%d] error!", fd);
//               }
//                 users_[fd].Close();
//                 //SendError_(fd, "Server busy!");
//                   LOG_WARN("Client is full");
//                   return;
//         }//所以临界的删去
//         if (timeoutMS_>0) {
//             //timer_->add(fd,timeoutMS_,std::bind(&WebServer::CloseConn_,this,&users_[fd]));
//            timer_->add(fd,timeoutMS_,[this,fd](){
//                 CloseConn_(&users_[fd]);
//             });
//         }
//         SetFdNonblock(fd);
//         epoller_->AddFd(fd,EPOLLIN|connEvent_);
//         LOG_INFO("Client[%d] in",users_[fd].GetFd());
// }
 //3个deal为star内函数，on才是真正的函数
// void WebServer::DealListen_(){
//          sockaddr_in addr;
//          socklen_t len=sizeof(addr);
//         do {
//            int fd=accept(listenFd_,(sockaddr*)&addr,&len);
//            if(fd<=0){
//                 return;
//            }else if(HttpConn::userCount>=MAX_FD){//usercount时atomic
//                   SendError_(fd, "Server busy!");
//                   LOG_WARN("Client is full");
//                   return;
//            }
//            AddClient_(fd,addr);
//         }while (listenEvent_&EPOLLET);//连接链表
// }
// void WebServer::DealWrite_(HttpConn* client){
//         assert(client);
//         ExtentTime_(client);
//         //threadpool_->AddTask(std::bind(&WebServer::OnWrite_,this,client));
//         threadpool_->AddTask([this,client](){
//                 OnWrite_(client);
//         });
// }
// void WebServer::DealRead_(HttpConn* client){
//         assert(client);
//         ExtentTime_(client);
//         //threadpool_->AddTask(std::bind(&WebServer::OnRead_,this,client));
//         threadpool_->AddTask([this,client]{
//                   OnRead_(client);//=只能放在最前面[=],[=,this],不能单个修饰,[this,client]已经是值捕获
//                   //&可以修饰单个[&client],[&],不能修饰this，只能值捕捉
//         });
// }
// void WebServer::OnRead_(HttpConn* client){
//         assert(client);
//         int readErrno=0;
        
//         int ret=client->read(&readErrno);
//         if(ret<=0&&readErrno!=EAGAIN){
//            CloseConn_(&users_[client->GetFd()]);
//            return;
//         }
//         OnProcess(client);
// }
// void WebServer:: OnWrite_(HttpConn* client){
//         assert(client);
//         int wrerrno=0;
        
//         int ret=client->write(&wrerrno);
//         if(client->ToWriteBytes()==0){
//                 if (client->IsKeepAlive()) {
//                         //直接开始下一次处理，没有才epollin
//                        OnProcess(client);
//                        return;
//                 }
//         }
//         else if(ret<0){
//             if(wrerrno == EAGAIN) {
//             /* 继续传输 */
//             epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);//返回again代表缓冲区满
//             return;
//         }
//      }
//      CloseConn_(client);
// }
// void WebServer::OnProcess(HttpConn* client){
//         if (client->process()) {
//         epoller_->ModFd(client->GetFd(),connEvent_|EPOLLOUT);
//         }
//         else {
//            epoller_->ModFd(client->GetFd(), connEvent_|EPOLLIN);
//         }
// }
// void WebServer::ExtentTime_(HttpConn* client){//额外时间
//         assert(client);
//         if(timeoutMS_>0){
//         timer_->adjust(client->GetFd(),timeoutMS_);
//         }
// }
// void WebServer::SendError_(int fd, const char*info){
//         assert(fd>0);
//         int ret=send(fd,info,sizeof(info),0);
//         if(ret<0){
//                 LOG_WARN("error to client[%d] error!", fd);
//         }
//        close(fd);
// }

// void WebServer::CloseConn_(HttpConn* client){
//     assert(client);
//     //LOG_INFO("Client[%d] quit!", client->GetFd());
//     epoller_->DleFd(client->GetFd());
//     client->Close();
    
// }

//  int WebServer::SetFdNonblock(int fd){
//         assert(fd>0);
//         return  fcntl(fd,F_SETFL,fcntl(fd, F_GETFL)|O_NONBLOCK);
//  }