#pragma once
#include "buffer.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "session.h"
#include <mutex>
#include <netinet/in.h>
enum class ProcessResult {
    NeedRead,
    ReadyWrite,
    NeedAuth,
    Upload,
    Download
};
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
    
    void process();
//    void processAuth() {
//     request_.DoAuth();
//     makeResponse(HttpRequest::ParseResult::Complete);
// }
   //判断是否为post和可解析文本，并初始化name，pwd
   void preAuth(){
       request_.DoAuth();
   }
   //查询或插入
    bool SqlQuary(){
        return request_.SqlQuary();
    }
    //加密或验证
    bool ar_hash_and_versity(){ 
        return  request_.ar_hash_and_versity();
       
    }
    bool is_login(){
        return request_.authuser.getIslogin();
    }
     void is_success(){
        request_.is_success();
     }
    void makeResponse(HttpRequest::ParseResult  sta);
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
    bool  versityToken(){
        return  Session::Intense()->versityToken(request_.GetPost("cookies"));
    }

    static bool isET;
     static const char* srcDir;
    static std::atomic<int> userCount;
    ProcessResult sta{};
private:
    mutable std::mutex io_mtx_;

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