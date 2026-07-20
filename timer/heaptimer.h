#pragma once
#include <list>
#include <vector>
#include <unordered_map>
#include <functional> 
#include <chrono>

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::steady_clock Clock;//单调时钟，不会因为系统时间被修改而失效
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;//时间戳
struct TimerNode {
    int slot;//槽
    int fd;//在adjust时，删除旧节点
     int rounds;//第几轮
    TimeoutCallBack cb;
    
};
typedef std::list<TimerNode>  Bucket;
class HeapTimer {
public:
    HeapTimer() { time_wheel.resize(slot_count);}

    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();
    void del_(int fd);
private:
    //void del_(int fd);
    
    // void siftup_(size_t i);

    // bool siftdown_(size_t index, size_t n);

    // void SwapNode_(size_t i, size_t j);
    int current_slot{0};//当前的slot
    int slot_time{200};//ms
    int slot_count{512};
    TimeStamp current_time{Clock::now()};
    int temp_time{0};//每次经过的时间不一定时slot的整数，将多余的存储起来，以免精度逐渐失效
    std::vector<Bucket> time_wheel;
    std::unordered_map<int,Bucket::iterator> ref_;//fd映射迭代器
};