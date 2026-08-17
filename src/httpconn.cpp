 #include "httpconn.h"
#include "download.h"
#include "httprequest.h"
 #include "log.h"
#include "session.h"
#include "upload.h"
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
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
// bool HttpConn::prase(){
//      return request_.parse(readBuff_);
// }
void HttpConn::process(){
     
     auto result=request_.parse(readBuff_);
      responseResult ret;
     //将所有的剩余数据调到前方，用来控制缓存区大小
     readBuff_.adjust_pos();
     //这里返回的是process的状态，让reactor知道是该读写，登录/注册，上传，下载
     switch (result) {
        case  HttpRequest::ParseResult::Incomplete:
              sta= ProcessResult::NeedRead;
              return;
       case  HttpRequest::ParseResult::NeedAuth:
              sta= ProcessResult::NeedAuth;
             
              return;       
        case  HttpRequest::ParseResult::Upload:
             // sta= ProcessResult::;
              if (!file.inited) {
                 file.inited=true;
                 request_.para_up_File(file);
                 //文件重传，验证失败
                 if (!versityToken(file.get_user_id())||!file.init_fileds()) {
                    ret=responseResult::ServerError;
                    break;
                 }
              }
              sta=ProcessResult::Upload;
              return;
        case  HttpRequest::ParseResult::Download:
             if (!versityToken(d_file.get_userid())) {
                   ret=responseResult::ServerError;
                    break;
             } 
            request_.para_down_File(d_file);
             d_file.inited=true;
              sta= ProcessResult::Download;
              return;   
        default:
              break;   
     }
      //太长以，格式错误，普通get/body直接做响应报文然后写
      makeResponse(ret);
   sta= ProcessResult::ReadyWrite;
}
void HttpConn::makeResponse(responseResult  sta){
      std::string path;
     int code=200;
     keepAlive_=false;

    switch (sta) {
       case responseResult::Complete:
       case responseResult::Download:
       case responseResult::Upload:
       case responseResult::Auth:
              path=request_.getpath();
            keepAlive_=request_.IsKeepAlive();
            code=200;
            //粘包，不 readBuff_.RetrieveAll();
            break;
       case responseResult::PayloadTooLarge:
           path="/413.html";
           code=413;
           readBuff_.RetrieveAll();
           break;
       case responseResult::ServerError:
           path="/500.html";
           code=500;
           readBuff_.RetrieveAll();
           break;
       case responseResult::BadRequest:
          path="/400.html";
           code=400;
           readBuff_.RetrieveAll();
           break;
        case responseResult::NotFound:
         path="/404.html";
           code=404;
           break;
        case responseResult::Unauthorized:
        //上传时好像要清理，下载时不要
         path="/401.html";
           code=401;
           readBuff_.RetrieveAll();
           break;
    }
    // 本次请求已经形成响应，解析器状态重置；readBuff_ 中未消费的下一请求字节会保留。
     request_.Init();
     //只有403在MakeResponse中用读权限判断给出
     response_.Init(srcDir,path,keepAlive_,code);
     //登录/注册
    if (sta==responseResult::Auth) {
         auto tokens=std::move( Session::Intense()->gettoken(request_.GetPost("username"))); 
        //  if (!tokens) {
        //此时应该将code转换为503  Service Unavailable重新登录
        //path也要更换
        //has——cookies=false
        //  }
        response_.set_filed("cookies",tokens.value());
    }else if (sta==responseResult::Download) {
          response_.set_filed("filename",d_file.get_filename());
          response_.set_filed("Content-Length: ",std::to_string(d_file.get_content_length()));
    }
        //普通报文
         response_.MakeResponse(writeBuff_,sta);
    

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
    do{
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
           *saveErrno=errno;
            break;
        }
        if(len==0){
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
            break;
        }
     }while (isET||ToWriteBytes()>10240);
     return len;
}

//  bool HttpConn::upload_file(int file_fd){
//        //因为结束符不一定是连贯的
//        int safe_size=readBuff_.ReadableBytes();
//        bool is_end=std::string(readBuff_.BeginWrite()-safe_size,readBuff_.BeginWrite())=="--"+request_.GetPost("boundary")+"--";
//        if (!is_end) {
//               //最极限时只有一个没到
//               safe_size-=4+request_.GetPost("boundary").size()+1;
//        }
//        file.incr_ready_write_size(safe_size);
//        int saveErrno=0;
//        int len=readBuff_.WriteFd(file_fd, saveErrno, safe_size);
//        file.incr_writed_size(len);
//        //此时分为不是end且是否写完，是end是否写完
//        if (!is_end) {
//          //不是end且是否写完都needread
//            return false;
//        }else if (is_end&&len!=safe_size) {
//             //是end但是没有写完
            
//        }{
       
//        }
//  }
//  ProcessResult HttpConn::handle_upload_file(){
//         if (!std::filesystem::is_directory("data/tmp")) {
//              std::filesystem::create_directory("data/tmp");
//         }
//         //返回值可以有成功，失败，文件重传
//         std::string tmp_file("data/tmp"+std::to_string(file.get_user_id())+"_"+file.get_filename());
//        int file_fd=::open(
//          tmp_file.c_str(), 
//          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
//           0600);
//        //  O_WRONLY   只写
//        // O_CREAT    文件不存在就创建
//        // O_EXCL     文件已存在则失败，避免覆盖
//         // O_CLOEXEC  exec 时自动关闭
//           // 0600       只有服务器进程所属用户可读写
//         bool ret=upload_file(file_fd);
//  }
void HttpConn:: Parseauth(){
       request_.paraAuth(authuser);
}
void HttpConn::ParseFile() {
    request_.para_up_File(file);
}
//  //查询或插入
// bool HttpConn::SqlQuary(){
//      return authuser.SqlQuary();
// }
// //加密或验证
// bool HttpConn::ar_hash_and_versity(){ 
//     return  authuser.ar_hash_and_versity();
// }
//  bool HttpConn::is_login(){
//      return authuser.getIslogin();
//  }
bool HttpConn::Auth_ar_and_sqlquary(){
     return authuser.Auth_ar_and_SqlQuary();
}
void HttpConn::set_Auth_html(){
     request_.set_Auth_html();
}
void HttpConn::set_upload_html(){
     request_.set_upload_html();
}
 bool HttpConn::IsKeepAlive() const {
     return keepAlive_;
}
bool HttpConn:: versityToken(size_t& user_id){
    //传进去user_id，通过这个获取
    auto tmp=std::move(request_.Getheader("cookie"));
    auto pos=tmp.find_first_of("=");

    return  Session::Intense()->versityToken(tmp.substr(pos+1),user_id);
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
DownloadResult HttpConn::handle_down(){
    if (d_file.inited) {
          //先打开文件，来判断响应报文的code
           d_file.inited=false;
     if (!d_file.openfile()) {
        
        makeResponse(responseResult::ServerError);
        //needwrite代表等待缓冲区，其他的直接写
        return  DownloadResult::Error;
     }
     //放在process时，当中途错误，仍然会创建错误的报文
      response_.set_isdownload(true);
     makeResponse(responseResult::Download);
     while (true) {
        int errno_=0;
     int ret=write(&errno_);
     //缓冲区读完
     if (ret<0&&(errno_==EWOULDBLOCK||errno_==EAGAIN)) {
           // return  DownloadResult::NeedWrite;
           //将file和buf混合一些，第一次可以大一点
           break;
     }else if (ret>0) {
           continue;
     }else {
       return  DownloadResult::Error;
     }
     }
    }
    return  d_file.handle_down(fd_);
}
