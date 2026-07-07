#pragma once
#include "epoll.h"
#include "eventLoopPool.h"
#include "eventloop.h"
#include "heaptimer.h"
#include "threadpool.h"
#include "httpconn.h"
#include <memory>
//Reator模式，不是主从reator（多个epoll），主从要采用多个线程池，2（从reator+业务处理）或者N（从reator+M个业务处理）
//非阻塞连接用select或者poll
class WebServer {
public:
    // WebServer(
    //     int port, int trigMode, int timeoutMS,int nums ,bool OptLinger, 
    //     int sqlPort, const char* sqlUser, const  char* sqlPwd, 
    //     const char* dbName, int connPoolNum, int threadNum,
    //     bool openLog, int logLevel, int logQueSize);
WebServer(
        int port, int trigMode, int timeoutMS,int nums ,bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);
    ~WebServer();
    void Start();

private:
    bool InitSocket_(); 
    void InitEventMode_(int trigMode);
    void AddClient_(int fd, sockaddr_in addr);
  
    void DealListen_();
    void DealWrite_(HttpConn* client);
    void DealRead_(HttpConn* client);

    void SendError_(int fd, const char*info);
    void ExtentTime_(HttpConn* client);
    void CloseConn_(HttpConn* client);

    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess(HttpConn* client);

    //分发clientfd
    void dealconn(int fd,sockaddr_in sa);
    static const int MAX_FD = 65536;

    static int SetFdNonblock(int fd);

    int port_;
    bool openLinger_;
    int timeoutMS_;  /* 毫秒MS */
    bool isClose_;
    int listenFd_;
    char* srcDir_;
    int caplicity;

    uint32_t listenEvent_;
    uint32_t connEvent_;//clientEvent_s
   
    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;

    std::unique_ptr<eventloop> loop;
    std::unique_ptr<eventLoopPool> eventpool;
    void conncallbak(int fd);
};