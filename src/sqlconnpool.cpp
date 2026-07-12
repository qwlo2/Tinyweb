#include "sqlconnpool.h"
#include "log.h"
#include <semaphore.h>
 SqlConnPool::SqlConnPool(){
    useCount_=0;
    freeCount_=0;
 }
    SqlConnPool::~SqlConnPool(){
        ClosePool();
    }

 SqlConnPool *SqlConnPool::Instance(){//单例
    static SqlConnPool connsql;
        return &connsql;
 }

 std::shared_ptr<MYSQL>SqlConnPool::GetConn(){
           if (!is_inited) {
               return nullptr;
           }
            std::shared_ptr<MYSQL> sql_conn;
            MYSQL* sql=nullptr;

                sem_wait(&semId_);
            std::lock_guard<std::mutex> lock(mutex_);
           

            sql=connQue_.front();
            connQue_.pop();
            ++useCount_;
           --freeCount_;
             sql_conn.reset(sql,[this](MYSQL* sql){
                  FreeConn(sql);
             });
            return sql_conn;
    }
    void SqlConnPool::FreeConn(MYSQL * conn){
           std::lock_guard<std::mutex> lock(mutex_);
           connQue_.push(conn);
           --useCount_;
           ++freeCount_;
           sem_post(&semId_);
    }
    int SqlConnPool::GetFreeConnCount(){
        std::lock_guard<std::mutex>  lock(mutex_);
         return  freeCount_;
    }
    void SqlConnPool::ClosePool(){
       
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connQue_.empty()) {
            auto item=connQue_.front();
            connQue_.pop();
            mysql_close(item);
        }
        mysql_library_end();
        sem_destroy(&semId_);
    }
    void SqlConnPool::Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize){
              assert(connSize > 0);
              MAX_CONN_=connSize;
            sem_init(&semId_,0,MAX_CONN_);
             for(int i=0;i<connSize;i++)
             {
                  MYSQL* sql=nullptr;//null分配新地址，mysql*重置资源，未初始化可能会报错（有无全访问地址）
            //       unsigned int timeout = 3;
            //   mysql_options(sql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
                  sql=mysql_init(sql);
                  if(!sql){
                    LOG_ERROR("mysql init error");
                   // ClosePool();
                      return;
                  }
                  sql=mysql_real_connect(sql, host, user, pwd,dbName,port,nullptr,0);
                  if(!sql){
                    LOG_ERROR("mysql connect error");
                   // ClosePool();
                    return;
                  }
                 // std::lock_guard<std::mutex> lock(mutex_);
                  connQue_.push(sql);
             }
            is_inited=true;
              LOG_INFO("Connecting MySQL: host=%s, port=%d, user=%s, db=%s", host, port, user, dbName);
            
            //0代表线程间使用，1代表进程间使用
            //MAX_CONN_代表
            // 信号量值大于 0：减 1，然后继续执行；
            // 信号量值等于 0：当前线程阻塞，等待其他线程增加信号量。

    }
