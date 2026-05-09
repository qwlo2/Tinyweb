#include "sqlconnpool.h"
#include "log.h"
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
            std::shared_ptr<MYSQL> sql_conn;
            MYSQL* sql=nullptr;

            std::lock_guard<std::mutex> lock(mutex_);
            if(connQue_.empty()){
                LOG_WARN("SqlConnPool busy!")
                return nullptr;
            }
            sem_wait(&semId_);
            sql=connQue_.front();
            connQue_.pop();
             sql_conn.reset(sql,[this](MYSQL* conn){
                std::lock_guard<std::mutex> lock(mutex_); 
                    connQue_.push(conn);
                    sem_post(&semId_);
             });
            return sql_conn;
    }
    void SqlConnPool::FreeConn(MYSQL * conn){
           std::lock_guard<std::mutex> lock(mutex_);
           connQue_.push(conn);
           sem_post(&semId_);
    }
    int SqlConnPool::GetFreeConnCount(){
        std::lock_guard<std::mutex>  lock(mutex_);
         return  connQue_.size();
    }
    void SqlConnPool::ClosePool(){
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connQue_.empty()) {
            auto item=connQue_.front();
            connQue_.pop();
            mysql_close(item);
        }
        mysql_library_end();
    }
    void SqlConnPool::Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize){
              assert(connSize > 0);
             for(int i=0;i<connSize;i++)
             {
                  MYSQL* sql=nullptr;//null分配新地址，mysql*重置资源，未初始化可能会报错（有无全访问地址）
            //       unsigned int timeout = 3;
            //   mysql_options(sql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
                  sql=mysql_init(sql);
                  if(!sql){
                    LOG_ERROR("mysql init error");
                    assert(sql);
                  }
                  sql=mysql_real_connect(sql, host, user, pwd,dbName,port,nullptr,0);
                  if(!sql){
                    LOG_ERROR("mysql connect error");
                
                  }
                 // std::lock_guard<std::mutex> lock(mutex_);
                  connQue_.push(sql);
             }

              LOG_INFO("Connecting MySQL: host=%s, port=%d, user=%s, db=%s", host, port, user, dbName);
            MAX_CONN_=connSize;
            sem_init(&semId_,0,MAX_CONN_);//0代表线程间使用，1代表进程间使用
    }
