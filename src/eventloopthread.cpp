#include "eventloopthread.h"
#include "eventloop.h"
#include <mutex>
#include <string>
#include <thread>

eventloopthread::eventloopthread(int timeoutms,int port,int event):timeoutMs(timeoutms),port(port),event(event){}

void eventloopthread::start(){
     thread_=std::thread([this](){
             threadFunc();
     });
      std::unique_lock<std::mutex> lock(mutex_);
      //保证loop一定初始化
      cv.wait(lock,[this](){
           return loop!=nullptr ;
      });
     
     // return loop;
} 
void eventloopthread::threadFunc(){
    
    eventloop loop_(timeoutMs,port,event);
    {
        std::unique_lock<std::mutex> lcok(mutex_);
        loop=&loop_;
         loop->setwakefd();
    }
    cv.notify_one();
    //如果用智能指针，loop的所有权是主线程的，因为是主线程的reactor pool
    //应该要保证子线程的loop属于子线程，因此用局部变量
    loop_.loop();
    {    
        //线程析构
         std::unique_lock<std::mutex> lcok(mutex_);
          loop=nullptr;
    }
}
void eventloopthread::stop(){
    loop->stop();

}
 void eventloopthread::tojoin(){
    if (thread_.joinable()) {
         thread_.join();
    }
 }




