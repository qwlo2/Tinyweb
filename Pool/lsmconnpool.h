#pragma once

#include "lsm.h"
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore.h>
class lsmconnpool{
      public:
    static lsmconnpool *Instance();

     std::shared_ptr<lsm> GetConn();
    void FreeConn(lsm* sql);
    int GetFreeConnCount();

    void Init(const char* host, int port, int connSize);//datebase
    void ClosePool();

private:
    lsmconnpool()=default;
    ~lsmconnpool()=default;

    int MAX_CONN_{0};
    //冗余
    int useCount_{0};
    int freeCount_{0};
        std::queue<lsm*>   connQue_;
    std::mutex mutex_;
    sem_t semId_;
    bool is_inited_{false};
};