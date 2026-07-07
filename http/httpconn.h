#pragma once
#include "buffer.h"
#include "httprequest.h"
#include "httpresponse.h"
#include <netinet/in.h>
class HttpConn {
public:
    HttpConn();

    ~HttpConn();

    void init(int sockFd, const sockaddr_in& addr);

    ssize_t read(int* saveErrno);

    ssize_t write(int* saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char* GetIP() const;
    
    sockaddr_in GetAddr() const;
    
    bool process();
    
    int ToWriteBytes() const {
        size_t bytes = 0;
        for(int i = 0; i < iovCnt_; ++i) {
            bytes += iov_[i].iov_len;
        }
        return static_cast<int>(bytes);
    }

    bool IsKeepAlive() const {
        return keepAlive_;
    }

    static bool isET;
     static const char* srcDir;
    static std::atomic<int> userCount;
    
private:
   
    int fd_;
    struct  sockaddr_in addr_;

    bool isClose_;
    bool keepAlive_;
    
    int iovCnt_;
    struct iovec iov_[2];
    
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;
    HttpResponse response_;
};