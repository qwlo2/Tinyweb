#pragma  once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
class ThreadPool{
    private:
     struct Pool{
        std::mutex mutex_;
        std::condition_variable cv;
        std::queue<std::function<void()>>  tasks;
        bool Isclose;
      };
      std::shared_ptr<Pool>  pool_;

      public:
             explicit ThreadPool(int threadcout=std::thread::hardware_concurrency()):pool_(std::make_shared<Pool>()){
               for(int i=0;i<threadcout;i++)
               {//初始化捕捉（可直接初始化变量），（）可省略，detach(分离),thread对象与工作线程分离，主线程退出，后台线程自动退出
                   std::thread ([pool=pool_](){
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
                                                   return  !pool->tasks.empty();
                                              });

                                               task=std::move(pool->tasks.front());
                                               pool->tasks.pop();
                                           }
                                           task();
                                        }
                                }
                   }).detach();
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
            pool_->tasks.emplace(std::forward<F>(task));
        }
        pool_->cv.notify_one();
    }

    ~ThreadPool(){
        if(static_cast<bool>(pool_)){
          {
            std::lock_guard<std::mutex> lock(pool_->mutex_);
            pool_->Isclose=false;
          }
          pool_->cv.notify_all();
        }
    }
};