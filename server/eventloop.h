#pragma  once
#include "acceptor.h"
#include "epoll.h"
#include "httpconn.h"
#include "heaptimer.h"
#include <memory>
#include <sys/epoll.h>
#include <thread>
#include <sys/eventfd.h>
class eventloop{
     private:
       static const int MAX_FD = 65536;

     //只用于c++内部比较，linux的不一定是整个id
     std::thread::id thread_id;
     std::unique_ptr<Epoller> ep;
     std::unique_ptr<acceptor> ac;
     int listenfd{-1};
    //main就是lisevent，从就是clientevent
     int timeoutMS;
     //析构时会关闭他们，不初始化可能查出随机值导致错误
     int wakeupfd{-1};
     int port;
      int event;
   
     std::mutex mutex_;
     std::unordered_map<int,std::shared_ptr<HttpConn>> httpcoon;
       //fd是进程共享，并且可以跨线程addepoll，但是为了不混乱，用任务队列串行执行（vctor）
      std::vector<std::function<void()>> pendindtask;
      std::unique_ptr<HeapTimer>   timer;
     std::atomic< bool > stopping{false};

      int SetFdNonblock(int fd);
      void SendError_(int fd, const char*info);
      void ExtentTime_(HttpConn* client);
      bool isCurrentConnection(int fd, const std::shared_ptr<HttpConn>& conn) const;
      public:
    //fd是进程共享，并且可以跨线程addepoll，但是为了不混乱，用queue执行
      eventloop(int timems,int port,int event);
       eventloop(eventloop* loop);
      ~eventloop();
      bool Is_in_own_thrad();
      //为了统一事件处理，无论自己还是其他的都通过任务队列来处理
      //因此要分辨是自己还是其他任务
      void push_and_do_task(const std::function<void()>& cb);
      void pushtask(const std::function<void()>& cb);
      void Do_task();
      //从rector的事件循坏
      void loop();
      //主reactor
      void Deallisten();
      bool initsock();
     
      void closeconn(int fd);

      void DealRead(int fd);
      void Onread(int fd,const std::shared_ptr<HttpConn> conn);

      void DealWrite(int fd);
      void Onwrite(int fd,const std::shared_ptr<HttpConn> conn);

      //处理登录/注册
      void handleAuth(int fd,const std::shared_ptr<HttpConn> conn);
      //处理上传
      void hadleUpload(int fd,const std::shared_ptr<HttpConn> conn);
      //处理下载
      void hadleDownload(int fd,const std::shared_ptr<HttpConn> conn);
       //处理分享
      void hadleShare(int fd,const std::shared_ptr<HttpConn> conn);
      //读写完调用它
      void process(int fd);

      void stop(){
        stopping=true;
        //此时可能wait，写eventfd，唤醒
        //main的为-1
         if (wakeupfd>0) {
         eventfd_t value=1;
        ::eventfd_write(wakeupfd,value);
         }
      }
      //分发fd后添加到epoll
      void AddClient_(int fd, sockaddr_in addr);
      void setdealconn(const std::function<void(int,sockaddr_in)>& cb);

      void setwakefd();
      int getwakefd();
      bool handlewakefd();
};