#pragma once
#include "Auth.h"
#include "buffer.h"
#include "download.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "upload.h"
#include <cstddef>
#include <netinet/in.h>
#include <string>
enum class ProcessResult {
    NeedRead,
    ReadyWrite,
    NeedAuth,
    Upload,
    Download,
    share,
    CloudData,
};
//如果每一种状态都在loop对于一种函数，太多了
//将其整理为一种状态（如share）
//然后通过sta在handle-share中进行处理对于的状态
enum class actual_ProcessResult{
     NeedAuth,
    Logout,
    Upload,
    Download,
     ShareCreate,//分享
    ShareCancel,
    ShareVerify,//验证提取码
    ShareDownload,
     ShareAccess, // GET /share/<token>
     ShareLogin,//这样就可以避免一个client获取
     FileList,
     FileDelete,
     ShareList,
     responseOnly,
     RangeError,
     Unauthorized,
     ServerError
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
    
    ProcessResult process();

   //判断是否为post和可解析文本，并初始化name，pwd
   void Parseauth();
   void ParseFile() ;

    bool Auth_ar_and_sqlquary();
   
     //文件上传
     bool upload_file(int file_fd);
      Upload handle_upload_file();
      DownloadResult handle_down();
      DownloadResult handle_response_write();
      bool get_download_inited();
     actual_ProcessResult get_sta();
    //分享
   // bool handle_share();
    bool handle_ShareAccess();
    bool handle_ShareCreate();
    bool handle_ShareVerify();
    bool handle_ShareDownload();
    bool handle_ShareLogin();
    bool handle_Logout();
    bool handle_ShareCancel();

    //显示功能和删除
    bool handle_FileList();
    bool handle_FileDelete();
    bool handle_ShareList();

   // bool handle_CloudData();
    //所有根据sta判断handle函数的类型
    bool handle_route();
    responseResult status_route(actual_ProcessResult& sta);

    void makeResponse(responseResult  sta);
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
    actual_ProcessResult sta{};
private:
   void Response_status_parse(responseResult& sta,std::string& path,int& code);
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
