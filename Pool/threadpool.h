#pragma  once
#include "staticfilecache.h"
#include "webserver.h"
#include <atomic>
#include <condition_variable>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
class ThreadPool{
    private:
     struct Pool{
        std::mutex mutex_;
        std::condition_variable cv;
        std::queue<std::function<void()>>  tasks;
         std::atomic<bool> Isclose{false};
      };
      std::shared_ptr<Pool>  pool_;
      std::vector<std::thread> threads;
      //可以添加一个vector，方便回收
      public:
             explicit ThreadPool(int threadcout=std::thread::hardware_concurrency()):pool_(std::make_shared<Pool>()){
               for(int i=0;i<threadcout;i++)
               {//初始化捕捉（可直接初始化变量），（）可省略，detach(分离),thread对象与工作线程分离，主线程退出，后台线程自动退出
                 threads.emplace_back(std::thread ([pool=pool_](){
                               while(true)
                               {
                                   if(pool->Isclose){
                                       break;
                                   }
                                   else {
                                           std::function<void()> task;
                                           {
                                              std::unique_lock<std::mutex>  lock(pool->mutex_);
                                              pool->cv.wait(lock,[pool](){
                                                   return  !pool->tasks.empty()||pool->Isclose;
                                              });
                                                 if (pool->Isclose) {
                                                       break;
                                                 }
                                               task=std::move(pool->tasks.front());
                                               pool->tasks.pop();
                                           }
                                           task();
                                        }
                                }
                   }))  ;
               }
      }
    //  bool empty(){
    //     return  pool_->tasks.empty();
    //  }
ThreadPool() = default;

ThreadPool(ThreadPool&&) = default;
//更通用，其中更多的是为了future，因此用了更多的语法
// template<typename F,typename ... Args>
// auto addtask(F&&f,Args...args)->std::future<typename std::invoke_result_t<F,Args...>::type>{
//            using task_type=typename std::invoke_result_t<F,Args...>::type;

//            auto task=std::make_shared<std::packaged_task<task_type()>> (std::bind(std::forward<F(f)>,std::forward<Args>(args)...));
//            std::future<task_type> res = task->get_future();
//        {
//             std::unique_lock<std::mutex> lock(pool_->mutex_);
//              pool_->tasks.emplace([task](){
//                    (*task)();
//              });//lamda执行完再析构packaged_task
//        }
//       pool_->cv.notify_one();
//         return res; 
// }

//底层接口
 template<class F>
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(pool_->mutex_);
            if (pool_->Isclose) {
              return;
            }
            pool_->tasks.emplace(std::forward<F>(task));
        }
        pool_->cv.notify_one();
    }

    ~ThreadPool()=default;
    void stop(){
         if(static_cast<bool>(pool_)){
            {
                std::lock_guard<std::mutex> locker(pool_->mutex_);
                 pool_->Isclose=true;
                  pool_->tasks={};
            }
          pool_->cv.notify_all();
          for(auto& it:threads){
            if (it.joinable()) {
                it.join();
            }
          }
        }
    }
    // static ThreadPool* init_Argon2id(){
    //     static  ThreadPool pool(WebServer::threadarnums);
    //     return  &pool;
    // }
    static ThreadPool* init_io(){
        static  ThreadPool pool(WebServer::threadionums);
        return  &pool;
    }
    static ThreadPool* init_Db(){
        static  ThreadPool pool(WebServer::threadDbnums);
        return  &pool;
    }
    static ThreadPool* init_File(){
        static  ThreadPool pool(WebServer::threadionums);
        return  &pool;
    }
};