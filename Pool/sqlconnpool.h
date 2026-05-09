#pragma once
#include <memory>
#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <semaphore.h>
class SqlConnPool {//数据库连接池
public:
    static SqlConnPool *Instance();

    std::shared_ptr<MYSQL>GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);//datebase
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;
    //冗余
    int useCount_;
    int freeCount_;

    std::queue<MYSQL *> connQue_;
    std::mutex mutex_;
    sem_t semId_;
};