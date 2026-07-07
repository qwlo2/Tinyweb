#pragma once

#include "eventloopthread.h"
#include <memory>
#include <vector>
class eventLoopPool{
    private:
    //不然回内存泄露
    std::vector<std::shared_ptr<eventloopthread>> eventloops;
    int caplicity;
    int timeoutMs;
    int port;
    int event;
    int nextid{-1};
    public:
    eventLoopPool(int nums,int timeoutms,int port,int event);
    ~eventLoopPool();
   void startloopPool();
   void setcaplicity(int nums);
   void stop();
   std::shared_ptr<eventloopthread>&  nextloop();
};