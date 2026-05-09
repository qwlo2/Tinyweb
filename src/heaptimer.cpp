#include "heaptimer.h"
//#include "log.h"
#include <cassert>
#include <arpa/inet.h> 
#include <chrono>
#include <cstddef>
#include <functional> 
//#include <iostream>
void HeapTimer::siftup_(size_t i){
     assert(i>=0&&i<heap_.size());
    while (i>0) {
        size_t j=(i-1)/2;
        if(j >= heap_.size()) {
            break;
        }
        if(heap_[j]<heap_[i]){
            break;//节点重载了<
        }
        SwapNode_(i,j);
        i=j;
    }
    //由于i是无符号数，因此会将与其进行运算的数全部转为无符号数，包括结果，
    // 因此当i==0时，i-1的结果是一个很大的整数，从而导致j超过了vector的size，从而导致了溢出。因此可以改成以下形式。
}

bool HeapTimer::siftdown_(size_t index, size_t n){//bool用以在更新时间后，判断向下还是向上
        assert(index>=0&&index<heap_.size());
        assert(n>=0&&n<=heap_.size());//n为局部调整，在del中，最后一个不纳入
        size_t i=index;
        size_t j=2*i+1;
        while (j<n) {
            if (j+1 < n && heap_[j+1]<heap_[j]) {
                j++;//当j为n左节点时，错误
            }
            if(heap_[i]<heap_[j]){
                 break;;
            }
            SwapNode_(i,j);
            i=j;
            j=2*i+1;
        }
        return i>index;
}

void HeapTimer::SwapNode_(size_t i, size_t j){
      assert(i>=0&&i<heap_.size());
      //LOG_DEBUG("j=%d",j);
       assert(j>=0&&j<heap_.size());
       std::swap(heap_[i],heap_[j]);
       ref_[heap_[i].id]=i;
       ref_[heap_[j].id]=j;
}
void HeapTimer::del_(size_t i){
      assert(!heap_.empty()&&i>=0&&i<heap_.size());
      size_t j=heap_.size()-1;
      if(i<j){
        SwapNode_(i,j);
        if(!siftdown_(i,j-1)){
            siftup_(i);
        }
      }
      ref_.erase(heap_.back().id);
      heap_.pop_back();
}
void HeapTimer::adjust(int id, int timeout){
    // assert(!heap_.empty() && ref_.count(id) > 0);
       if (heap_.empty() || ref_.count(id) == 0) {
            return; }
         int index=ref_[id];
         heap_[index].expires=Clock::now()+MS(timeout);//将timeout转为毫秒再加入高精度钟
         if(!siftdown_(index,heap_.size())){
            siftup_(index);//调整后时间可能变大，变小
        }
}

void HeapTimer::add(int id, int timeOut, const TimeoutCallBack& cb){
    assert(id>0);
    if(ref_.count(id)==0){
        ref_.emplace(id,heap_.size());
        heap_.push_back({id,Clock::now()+MS(timeOut),cb});
        siftup_(heap_.size()-1);
    }
    else {//由于fd复用，才有这个
        int index=ref_[id];
        heap_[index].expires=Clock::now()+MS(timeOut);
        heap_[index].cb=cb;
        if(!siftdown_(index,heap_.size())){
            siftup_(index);//调整后时间可能变大，变小
        }
    }
}
//基本没用
void HeapTimer::doWork(int id){//do后del
    if (heap_.empty()||ref_.count(id)==0) {
         return;
    }
     int index=ref_[id];
     TimerNode node=heap_[index];
     node.cb();
    del_(index);
}

void HeapTimer::clear(){
    heap_.clear();
    ref_.clear();
}

void HeapTimer::tick(){//处理所有到时fd
   if(heap_.empty()) {
        return;
    }
    while (!heap_.empty()) {
          if(heap_[0].expires>Clock::now()){
            break;//std::chrono::duration_cast<MS>(heap_[0].expires-Clock::now()).count()>0
         }
         heap_[0].cb();
         pop();

    }
}

void HeapTimer::pop(){
    assert(!heap_.empty());
    del_(0);
}

int HeapTimer::GetNextTick(){//获取wait的时间
      tick();
      int res=-1;//一直阻塞
     if(!heap_.empty()){
         res=std::chrono::duration_cast<MS>(heap_[0].expires-Clock::now()).count();
         if(res<0){
            res=0;//立即返回
         }
     }
     return res;
}