#include "heaptimer.h"
//#include "log.h"
#include <arpa/inet.h> 
#include <chrono>
#include <functional> 
//#include <iostream>
// void HeapTimer::siftup_(size_t i){
//      assert(i>=0&&i<heap_.size());
//     while (i>0) {
//         size_t j=(i-1)/2;
//         if(j >= heap_.size()) {
//             break;
//         }
//         if(heap_[j]<heap_[i]){
//             break;//节点重载了<
//         }
//         SwapNode_(i,j);
//         i=j;
//     }
//     //由于i是无符号数，因此会将与其进行运算的数全部转为无符号数，包括结果，
//     // 因此当i==0时，i-1的结果是一个很大的整数，从而导致j超过了vector的size，从而导致了溢出。因此可以改成以下形式。
// }

// bool HeapTimer::siftdown_(size_t index, size_t n){//bool用以在更新时间后，判断向下还是向上
//         assert(index>=0&&index<heap_.size());
//         assert(n>=0&&n<=heap_.size());//n为局部调整，在del中，最后一个不纳入
//         size_t i=index;
//         size_t j=2*i+1;
//         while (j<n) {
//             if (j+1 < n && heap_[j+1]<heap_[j]) {
//                 j++;//当j为n左节点时，错误
//             }
//             if(heap_[i]<heap_[j]){
//                  break;;
//             }
//             SwapNode_(i,j);
//             i=j;
//             j=2*i+1;
//         }
//         return i>index;
//}

// void HeapTimer::SwapNode_(size_t i, size_t j){
//       assert(i>=0&&i<heap_.size());
//       //LOG_DEBUG("j=%d",j);
//        assert(j>=0&&j<heap_.size());
//        std::swap(heap_[i],heap_[j]);
//        ref_[heap_[i].id]=i;
//        ref_[heap_[j].id]=j;
// }
// void HeapTimer::del_(size_t i){
     
// }
//不在延续，而是直接调整为timeout
void HeapTimer::adjust(int id, int timeout){
    auto& it=ref_[id];
         //add时的起点应该是此时的时间所对应的计时器
        int now_time=std::chrono::duration_cast<MS>(std::chrono::steady_clock::now()-current_time).count();
      
        int sumTime=timeout+now_time+temp_time;
       TimerNode node;
        node.cb=it->cb;
        node.fd=id;
        //因为tick时会计算temp-time，因此这里也要加上，还要向上取整
        //保证一定超时
       int node_slot=(sumTime+slot_time-1)/slot_time;
      //如果正好是一轮，不要+1,因为，算的是currnt要走的路程，
       node.rounds=(node_slot-1)/slot_count;
       node.slot=(node_slot+current_slot)%slot_count;

       time_wheel[node.slot].emplace_front(node);
       time_wheel[it->slot].erase(it);
      ref_[id]=time_wheel[node.slot].begin();

}
void HeapTimer::del_(int fd){
    if (!ref_.contains(fd)) {
       return;
    }
     auto it=ref_[fd];
    ref_.erase(fd);
    time_wheel[it->slot].erase(it);
}
void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb){
       if (ref_.empty()) {
           current_time=std::chrono::steady_clock::now();
       }
        TimerNode node;
        node.cb=cb;
        node.fd=id;
        //因为tick时会计算temp-time，因此这里也要加上，还要向上取整
        //保证一定超时
        //add时的起点应该是此时的时间所对应的计时器
        int now_time=std::chrono::duration_cast<MS>(std::chrono::steady_clock::now()-current_time).count();
        
        int sumTime=timeout+now_time+temp_time;
         int node_slot=(sumTime+slot_time-1)/slot_time;
      //如果正好是一轮，不要+1,因为，算的是currnt要走的路程，
       node.rounds=(node_slot-1)/slot_count;
       node.slot=(node_slot+current_slot)%slot_count;

       time_wheel[ node.slot].emplace_front(node);
       ref_.emplace(id, time_wheel[ node.slot].begin());
}
// //基本没用
// void HeapTimer::doWork(int id){//do后del
//     if (heap_.empty()||ref_.count(id)==0) {
//          return;
//     }
//      int index=ref_[id];
//      TimerNode node=heap_[index];
//      node.cb();
//     del_(index);
// }

void HeapTimer::clear(){
    time_wheel.clear();
    ref_.clear();
}

void HeapTimer::tick(){//处理所有到时fd
   if(ref_.empty()) {
     current_time=Clock::now();
        return;
    }
    int time=std::chrono::duration_cast<MS>(std::chrono::steady_clock::now()-current_time).count();
    int slots=(time+temp_time)/slot_time;
     temp_time=(time+temp_time)%slot_time;
     for (int i=1;i<=slots;++i) {
        auto& tmp=time_wheel[(i+current_slot)%slot_count];
        for (auto it = tmp.begin(); it != tmp.end();) {
          if (it->rounds > 0) {
            --it->rounds;
            ++it;
            continue;
          }

          int fd = it->fd;
          auto cb = it->cb;
          it = tmp.erase(it);
          ref_.erase(fd);
          cb();
        }
     }
    current_slot=(slots+current_slot)%slot_count;
    current_time=Clock::now();
}

// void HeapTimer::pop(){
//     //assert(!heap_.empty());
//     del_(0);
// }

int HeapTimer::GetNextTick(){//获取wait的时间
      tick();
      int res=-1;//一直阻塞
     if(!ref_.empty()){
         //res=std::chrono::duration_cast<MS>(heap_[0].expires-Clock::now()).count();
         res=slot_time-temp_time;
         if(res<0){
            res=0;//立即返回
         }
     }
     return res;
}