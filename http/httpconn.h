#pragma once
#include "Auth.h"
#include "buffer.h"
#include "download.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "session.h"
#include "upload.h"
#include <cstddef>
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
   void Parseauth();
   void ParseFile() ;
   //查询或插入
    bool SqlQuary();
    //加密或验证
    bool ar_hash_and_versity();
    bool is_login();
     void is_success();
     //文件上传
     bool upload_file(int file_fd);
      Upload handle_upload_file();
      DownloadResult handle_down();

    void makeResponse(HttpRequest::ParseResult  sta);
    int ToWriteBytes() const {
        size_t bytes = 0;
        for(int i = 0; i < iovCnt_; ++i) {
            bytes += iov_[i].iov_len;
        }
        return static_cast<int>(bytes);
    }

    bool IsKeepAlive() const ;
    bool  versityToken(size_t& user_id);

    static bool isET;
     static const char* srcDir;
    static std::atomic<int> userCount;
    ProcessResult sta{};
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

    //登录/注册
    Auth authuser;
    //文件下载上传
    UploadFile file;
    Download d_file;
};