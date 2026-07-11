#include "lsmconnpool.h"
#include "log.h"
#include "lsm.h"
#include <memory>
#include <mutex>
#include <semaphore.h>



 lsmconnpool *lsmconnpool::Instance(){
    static lsmconnpool lsmpool;
    return &lsmpool;
}

std::shared_ptr<lsm> lsmconnpool::GetConn(){
    // std::unique_lock<std::mutex> lock(mutex_);
     if (!is_inited_) {
        return nullptr;
     }
    //
     sem_wait(&semId_);
     //只有减一的线程会获取锁
     std::unique_lock<std::mutex> lcok(mutex_); 
     lsm* tmp=connQue_.front();
     connQue_.pop();
      return std::shared_ptr<lsm>(tmp,[this](lsm* tmp){
              FreeConn(tmp);
      });
     //  std::shared_ptr<lsm> ans;
     //  ans.reset(tmp,[this,tmp](){
     //          FreeConn(tmp);
     //  } )
}
void lsmconnpool::FreeConn(lsm* sql){
         // std::unique_lock<std::mutex> lock(mutex_);
         std::unique_lock<std::mutex> lock(mutex_);
          connQue_.emplace(sql);
           --useCount_;
           ++freeCount_;
           sem_post(&semId_);

}
int lsmconnpool::GetFreeConnCount(){
    return freeCount_;
}

void lsmconnpool::Init(const char *host, int port, int connSize) {
         MAX_CONN_=connSize;
         sem_init(&semId_,0,MAX_CONN_);
        //0代表线程间使用，1代表进程间
        for(int i=0;i<connSize;++i){
             auto sql=lsm::lsm_init();
             
             if (!sql) {
                  LOG_ERROR("lsmsqlpool creat error");
                 /// ClosePool();
                  return;
             }
             int ret=lsm::lsm_real_connect(sql,host,port);
             if (ret!=0) {
              LOG_ERROR("lsmsqlpool connect error");
                // ClosePool();
                  return;
             }
             //connQue_.push_back(sql);
             connQue_.emplace(sql);
        }
        is_inited_=true;
       
}

void lsmconnpool::ClosePool(){
     //这里不用判断isinited，因为可能有部分被初始化
     std::unique_lock<std::mutex> lock(mutex_);
     while (!connQue_.empty()) {
          auto lsm=connQue_.front();
          connQue_.pop();
          lsm::connclose(lsm);
          delete lsm;
     }
      sem_destroy(&semId_);
}