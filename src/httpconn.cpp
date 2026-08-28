 #include "httpconn.h"
#include "download.h"
#include "file_shared.h"
#include "httprequest.h"
#include "httpresponse.h"
 #include "log.h"
#include "session.h"
#include "sha256.h"
#include "upload.h"
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <hiredis/hiredis.h>
#include <hiredis/read.h>
#include <netinet/in.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>    
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <utility>
 bool HttpConn::isET;
 const char* HttpConn::srcDir;
 std::atomic<int> HttpConn::userCount;
HttpConn::HttpConn(){
     fd_=-1;
     addr_={0};
     isClose_=true;
      keepAlive_=false;
      iovCnt_=0;
      
      std::memset(iov_,0,sizeof(iov_));
}

HttpConn::~HttpConn(){
    Close();
}
//因为要fd复用，因此单独一个init函数
void HttpConn::init(int sockFd, const sockaddr_in& addr){
    assert(sockFd>0);
    userCount++;
       fd_=sockFd;
       addr_=addr;
       //释放扩展的缓冲区大小
       readBuff_={};
       writeBuff_={};
       //response_.UnmapFile();
       request_.Init();
       keepAlive_=false;
       iovCnt_=0;
        //这3个的要是否资源因为下一次报文不一定与文件有关
        authuser.init();
        file.init();
        d_file.init();

       std::memset(iov_,0,sizeof(iov_));
       isClose_=false;
       LOG_INFO("Client[%d](%s:%d) in,usercout:%d",fd_,GetIP(),addr_.sin_port,(int)userCount);
}


void HttpConn::Close(){

        if(isClose_){
            return;
        }
        response_.UnmapFile();
        isClose_=true;
        userCount--;
       // oldFd=fd_;
       
    if(fd_>=0){
        ::close(fd_);
         fd_=-1;
        LOG_INFO("Client[%d] quit!",fd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const{
    return fd_;
}

int HttpConn::GetPort() const{
    return  addr_.sin_port;
}

const char* HttpConn::GetIP() const{
    //   char ch[INET_ADDRSTRLEN];
    //   bzero(ch,sizeof(ch));//memest
    // const char* res=inet_ntop(AF_INET,&addr_.sin_addr,ch , sizeof(ch));
    // if (res == nullptr) {
    // // 处理错误，如打印日志
    // LOG_DEBUG("inet_ntop failed");
    // }
    //  return ch;
    return inet_ntoa(addr_.sin_addr);
}
sockaddr_in HttpConn::GetAddr() const{
     return addr_;
}

ProcessResult HttpConn::process(){
     
     auto result = request_.parse(readBuff_);
     responseResult ret;
    switch (result) {
     case ParseResult::Incomplete:
        return  ProcessResult::NeedRead;

     case ParseResult::BadRequest:
       ret = responseResult::BadRequest;
       break;

     case ParseResult::PayloadTooLarge:
       ret = responseResult::PayloadTooLarge;
       break;

     case ParseResult::Complete:
       ret=responseResult::Complete;
       break;
     }
    if (result != ParseResult::Complete) {
       makeResponse(ret);
       return  ProcessResult::ReadyWrite;
   }

     //将所有的剩余数据调到前方，用来控制缓存区大小
     readBuff_.adjust_pos();
    switch (request_.route()) {
        case  RouteType::NeedAuth:
                    sta =actual_ProcessResult::NeedAuth;
                 return   ProcessResult::NeedAuth;

        case RouteType::Upload:
          // sta= ProcessResult::;
          if (!file.inited) {

            // 文件重传，验证失败
             if (!versityToken(file.get_user_id()) ) {
              ret = responseResult::Unauthorized;
              break;
            }
              file.inited = true;
            request_.para_up_File(file);
             if ( !file.init_fileds()) {
              ret = responseResult::ServerError;
              break;
            }
          }
          sta =actual_ProcessResult::Upload;
          return   ProcessResult::Upload;
    
        case RouteType::Download:
          // 文件重传，验证失败
             if (!versityToken(d_file.get_userid()) ) {
              ret = responseResult::Unauthorized;
              break;
            }
              d_file.inited = true;
          request_.para_down_File(d_file);
          sta =actual_ProcessResult::Download;
           return  ProcessResult::Download;

        case RouteType::ShareCreate:
             sta=actual_ProcessResult::ShareCreate;
                 return ProcessResult::share;

        case RouteType::ShareAccess:
               sta=actual_ProcessResult::ShareAccess;
               return ProcessResult::share;
        case RouteType::ShareVerify:
               sta=actual_ProcessResult::ShareVerify;
                return ProcessResult::share;
        case RouteType::ShareLogin:
               sta=actual_ProcessResult::ShareLogin;
                return ProcessResult::share;
        case RouteType::ShareDownload:{
           size_t user_id=0;
               //这里的uer-id是冗余的，在之后下载需要的userid是file所属的，不是账号本身的
               if (!versityToken(user_id) ) {
                  ret = responseResult::Unauthorized;
                  break;
               }
               sta=actual_ProcessResult::ShareDownload;
               return ProcessResult::share;
        }
        case RouteType::Normal:
           // 普通静态资源,sta并未在init中重置
            sta={};
            break;
     }
      //太长以，格式错误，普通get/body直接做响应报文然后写
      makeResponse(ret);
      return  ProcessResult::ReadyWrite;
}
 void HttpConn::Response_status_parse(responseResult& sta,std::string& path,int& code){
     switch (sta) {
       case responseResult::Complete:
            path=request_.getpath();
            //粘包，不 readBuff_.RetrieveAll();
            break;
       case responseResult::Download:
             //path=request_.getpath();
            //粘包，不 readBuff_.RetrieveAll();
            if (d_file.get_range_valid() ) {
               code=206;
            }
            break;
        case responseResult::RangeError:
            code=416;
            break;
       case responseResult::Upload:
           path="/upload_success.html";
            //粘包，不 readBuff_.RetrieveAll();
            break;
       case responseResult::Auth:
             path="/welcome.html";
            //粘包，不 readBuff_.RetrieveAll();
            break;
        case responseResult::ShareCreate:
            path="/welcome.html";
            break;
        case responseResult::ShareAccess:
        if (response_.get_filed("has_code")=="true") {
                path="/share_access_code.html";
        }else {
         path="/share_access.html";
        }
        
            break;
        case responseResult::ShareVerify:
           path="/share_access.html";
            break;
        case responseResult::ShareLogin:
           path="/share_login.html";
            break;
        case responseResult::ShareDownload:
            break;
       case responseResult::PayloadTooLarge:
           code=413;
           readBuff_.RetrieveAll();
           break;
       case responseResult::ServerError:
           code=500;
           readBuff_.RetrieveAll();
           break;
       case responseResult::BadRequest:
           code=400;
           readBuff_.RetrieveAll();
           break;
        case responseResult::NotFound:
           code=404;
           break;
        case responseResult::Unauthorized:
        //上传时好像要清理，下载时不要
           code=401;
           readBuff_.RetrieveAll();
           break;
    }
 }
void HttpConn::makeResponse(responseResult  sta){
      std::string path;
     int code=200;
    keepAlive_=request_.IsKeepAlive();
    Response_status_parse(sta,path,code);
    
     //只有403在MakeResponse中用读权限判断给出
     response_.Init(srcDir,path,keepAlive_,code);
     //登录/注册
    if (sta==responseResult::Auth||sta==responseResult::ShareLogin) {
         auto tokens=std::move( Session::Intense()->gettoken(request_.GetPost("username"))); 
        //  if (!tokens) {
        //此时应该将code转换为503  Service Unavailable重新登录
        //path也要更换
        //has——cookies=false
        //  }
        response_.set_filed("cookie",tokens.value());
    }else if (sta==responseResult::Download) {
      response_.set_filed("filename", d_file.get_filename());
       response_.set_filed("Accept-Ranges", "bytes");

       if (d_file.get_range_valid()) {
           response_.set_filed("range_valid", "true");
            response_.set_filed("Content-Range",
                          "bytes " + std::to_string(d_file.get_range_start()) +
                              "-" + std::to_string(d_file.get_range_end()) +
                              "/" + std::to_string(d_file.get_file_size()));
       }
      response_.set_filed("Content-Length: ",
                          std::to_string(d_file.get_content_length()));
    }else if (sta==responseResult::RangeError ) {
             response_.set_filed("Content-Range",
                          "bytes */" + std::to_string(d_file.get_file_size()));
    }
        //普通报文
         response_.MakeResponse(writeBuff_,sta);
   // 本次请求已经形成响应，解析器状态重置；readBuff_ 中未消费的下一请求字节会保留。
     request_.Init();
     file.init();
    // d_file.init();放在handle-down中，因为响应报文之后才是正式的d-file开始传输文件

     iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek());
     iov_[0].iov_len=writeBuff_.ReadableBytes();
     iovCnt_=1;
     if(response_.getFile()&&response_.getFileLen()>0){
          iov_[1].iov_base=response_.getFile();
          iov_[1].iov_len=response_.getFileLen();
          iovCnt_++;
     }
     LOG_DEBUG("filesize:%zu, iovCnt:%d, toWrite:%d", response_.getFileLen(), iovCnt_, ToWriteBytes());
}
ssize_t HttpConn::read(int* saveErrno){
    if(isClose_||fd_<0){
        *saveErrno=ECANCELED;
        return -1;
    }
    ssize_t len=0;
    ssize_t total=0;
    //在纯ET下，维持64kb缓冲区，当socket——buffer>64kb时重新注册会阻塞
    //而在EPOLLSHOT下回强制状态先为未读/可写，因此会再次通知不会阻塞
    do{
         if (readBuff_.WritableBytes() == 0) {
           // 本轮缓冲区已满，返回上层解析和消费
           break;
        }
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len>0){
            total+=len;
            continue;
        }
        if(len==0){
            return total;
        }
        
        if(*saveErrno==EAGAIN||*saveErrno==EWOULDBLOCK){
            //是否为第一次
            return total>0?total:len;
        }
        return len;
    }while(isET);
    return total;
}
//将iov的0的writerbuffer和1的file
//通过iov写入fd
ssize_t HttpConn::write(int* saveErrno){
     if(isClose_||fd_<0){
        *saveErrno=ECANCELED;
        return -1;
     }
     ssize_t len=-1;
     do {
        len=writev(fd_, iov_,iovCnt_);
        if (len<0) {
            //中断的时候继续，其他的直接返回
           if (errno==EINTR) {
              continue;
           }
           *saveErrno=errno;
            break;
        }
        if(len==0){
        //返回0代表，本次请求写入的总长度通常就是 0，不能用于判断连接或响应状态。
            break;
        }
       
        if(static_cast<size_t>(len)>iov_[0].iov_len){
            //专用于字节操作
              size_t write_len = len - iov_[0].iov_len;
              iov_[1].iov_base=(uint8_t*)iov_[1].iov_base+write_len;//uint8_t等价于 unsigned char，固定占 1 字节
              iov_[1].iov_len=iov_[1].iov_len-write_len;
              writeBuff_.RetrieveAll();
              iov_[0].iov_len=0;
        }
        else {
           size_t retrieve_len = std::min(static_cast<size_t>(len), writeBuff_.ReadableBytes());//防止意外
           iov_[0].iov_base=(uint8_t*)iov_[0].iov_base+retrieve_len;
           iov_[0].iov_len=iov_[0].iov_len-retrieve_len;
           writeBuff_.Retrieve(retrieve_len);
        }
         if(ToWriteBytes()==0){
         //这里判断了len==0的判断就失效了，因此才有了ret>0
         //通常要一直写完，直到缓冲区满或完成
            break;
        }
     }while (isET||ToWriteBytes()>10240);
     return len;
}

void HttpConn:: Parseauth(){
       request_.paraAuth(authuser);
}
void HttpConn::ParseFile() {
    request_.para_up_File(file);
}

bool HttpConn::Auth_ar_and_sqlquary(){
     return authuser.Auth_ar_and_SqlQuary();
}

 bool HttpConn::IsKeepAlive() const {
     return keepAlive_;
}
bool HttpConn:: versityToken(size_t& user_id){
    //传进去user_id，通过这个获取
    // auto tmp=std::move(request_.Getheader("cookie"));
    // auto pos=tmp.find_first_of("=");
   auto session = request_.get_cookie("session");
    if (!session){
         return false;
    }
   
    return  Session::Intense()->versityToken(session.value(),user_id);
}
Upload HttpConn ::handle_upload_file(){
     //由于是ET下，因此要一直读到ReadyWrite或者缓冲区完
     Upload res;
    while (true) {
     res=std::move( file.handle_upload_file(readBuff_));
     if (res==Upload::UploadError||res==Upload::ReadyWrite) {
             return  res;
     }
     int errno_=0;
     int ret=read(&errno_);
     //缓冲区读完
     if (ret<0&&(errno_==EWOULDBLOCK||errno_==EAGAIN)) {
            return  res;
     }else if (ret>0) {
           continue;
     }else {
       return  Upload::UploadError;
     }
    }
}
bool HttpConn::get_download_inited(){
     return d_file.inited;
}
DownloadResult HttpConn::handle_response_write(){
   
    while (true) {
        int errno_=0;
     int ret=write(&errno_);
      
     if (ToWriteBytes()==0) {//由于conn.write的设计，返回值为>0和-1+EWOULDBLOCK，或者错误；0被避免了
        //ret==0不能判断是否写完，要根据iov的长度来判断
          d_file.inited=false;
          return  DownloadResult::Finished;
     }else if (ret<0&&(errno_==EWOULDBLOCK||errno_==EAGAIN)) {
           //loop还要进行特殊处理，暂时未处理
             d_file.inited=true;
           return  DownloadResult::NeedWrite;

     }else {
        d_file.inited=false;
       return  DownloadResult::Error;
     }
     }
} 
DownloadResult HttpConn::handle_down(){
    if (d_file.inited) {
          //先打开文件，来判断响应报文的code
        auto tmp=std::move(d_file.openfile());
     if (tmp==DownloadResult::Error) {
        
        makeResponse(responseResult::ServerError);
        //needwrite代表等待缓冲区，其他的直接写
        return  DownloadResult::Error;
     }else if (tmp==DownloadResult::RangeError) {
         makeResponse(responseResult::RangeError);
         sta=actual_ProcessResult::responseOnly;
         //必须切换，不然在onwrite中还是会调用doanload
         return  DownloadResult::RangeError;
     }
     //放在process时，当中途错误，仍然会创建错误的报文
      makeResponse(responseResult::Download);

      if (auto ret=std::move(handle_response_write());ret!=DownloadResult::Finished) {
          return ret;
      }
    }
    auto tmp=std::move(d_file.handle_down(fd_));
    if (tmp==DownloadResult::Finished) {
       d_file.init();
    }
    return  tmp;
}


bool HttpConn::handle_share(){
    switch (sta) {
       case actual_ProcessResult::ShareCreate:
                    return handle_ShareCreate();
       case actual_ProcessResult::ShareAccess:
                    return handle_ShareAccess();
       case actual_ProcessResult::ShareVerify:
                    return handle_ShareVerify();
       case actual_ProcessResult::ShareDownload:
                    return handle_ShareDownload();
        case actual_ProcessResult::ShareLogin:
                    return handle_ShareLogin();
    }            
    return false;
}
bool HttpConn::handle_ShareCreate(){
    size_t user_id=0;
    if (!versityToken(user_id)) {
        return false;
    }
   auto res=std::move( File_shared::Instance()->share_file(request_.GetPost("code"),
                      user_id, request_.GetPost("filename"),request_.GetPost("expire_time")));
        if (!res) {
           return false;
        }
        response_.set_filed("share_token", res.value().first);
        response_.set_filed(
        "code",
        res->second == "NULL" ? "" : res->second
    );
        return true;
}
bool HttpConn::handle_ShareAccess(){
     auto res=std::move( File_shared::Instance()->vsersity_ShareAccess(request_.route_token()));
     if (res!="") {
         response_.set_filed("has_code", res);
         return true;
     }
     return false;
}
bool HttpConn::handle_ShareVerify(){
      return  File_shared::Instance()->versity_share_token(request_.route_token(),request_.GetPost("code"));
}
bool HttpConn::handle_ShareLogin(){
    request_.paraAuth(authuser);
   return   authuser.Auth_ar_and_SqlQuary();
}
bool HttpConn::handle_ShareDownload(){
    auto auth_hash="share_auth:"+sha256_hex(std::move(request_.route_token()));
    size_t file_id=0;
    if ( File_shared::Instance()->versity_doenload( file_id,auth_hash)) {
         if (!d_file.share_init(file_id)) {
              return false;
         }
         d_file.inited=true;
         sta=actual_ProcessResult::Download;
         return true;
    }
    return false;
}

responseResult HttpConn::status_route(actual_ProcessResult& sta){
       switch (sta) {
          case actual_ProcessResult::Download:
                  return responseResult::Download;
          case actual_ProcessResult::NeedAuth:
                  return responseResult::Auth;
          case actual_ProcessResult::Upload:
                   return responseResult::Upload;
          case actual_ProcessResult::ShareCreate:
                  return responseResult::ShareCreate;
          case actual_ProcessResult::ShareAccess:
                 return responseResult::ShareAccess;
          case actual_ProcessResult::ShareVerify:
                 return responseResult::ShareVerify;
          case actual_ProcessResult::ShareDownload:
                return responseResult::ShareDownload;
           case actual_ProcessResult::ShareLogin:
                return responseResult::ShareLogin;
       }
}
 actual_ProcessResult HttpConn::get_sta(){
     return sta;
 }
