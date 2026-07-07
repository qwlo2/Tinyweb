#pragma once
#include "eventloop.h"
#include <condition_variable>
#include <functional>

#include <mutex>
#include <thread>
class eventloopthread{
     private:
       //它指向的时线程函数中的局部变量，不要管析构
        eventloop* loop;

        std::thread  thread_;
        int timeoutMs;
        std::mutex mutex_;
        std::condition_variable cv;
        int port;
        int event;
  
     public:
       void start();
       void threadFunc();
       eventloopthread(int timeoutms,int port,int event);
       std::thread getthread();
       void stop();
       void tojoin();
       
       void pushtask(const std::function<void()>& cb_){
         loop->pushtask(cb_);
       }
       void addclient(int fd,sockaddr_in addr){
        loop->AddClient_(fd, addr);
       }
       int getwakefd(){
        return  loop->getwakefd();
       }
};