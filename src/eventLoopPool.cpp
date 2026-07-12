#include "eventLoopPool.h"
#include "eventloopthread.h"
#include <memory>
#include <sys/types.h>

eventLoopPool::eventLoopPool(int nums,int timeoutms,int port,int event_):caplicity(nums),timeoutMs(timeoutms),
port(port),event(event_){

}
eventLoopPool::~eventLoopPool(){
//  for (auto& it : eventloops) {
//       it->stop();
//  }
// for (auto& it : eventloops) {
//       it->tojoin();
//  }

}
void eventLoopPool::stop(){
    for (auto& it : eventloops) {
      it->stop();
 }
for (auto& it : eventloops) {
      it->tojoin();
 }
}
void eventLoopPool::startloopPool(){
      for (int i=0;i<caplicity;++i) {
           // eventloopthread loop(timeoutMs);
           //回析构
           auto loop=std::make_shared<eventloopthread>(timeoutMs,port,event);
          loop->start();
          
         eventloops.emplace_back(loop);
      }
}
void eventLoopPool::setcaplicity(int nums){
    caplicity=nums;
}
 std::shared_ptr<eventloopthread>&   eventLoopPool::nextloop(){
     ++nextid;
     if (nextid>=caplicity) {
        nextid=0;
     } 
     return  eventloops[nextid];
 }