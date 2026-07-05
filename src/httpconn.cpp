 #include "httpconn.h"
 #include "log.h"
#include <netinet/in.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <errno.h>    
 bool HttpConn::isET;
 const char* HttpConn::srcDir;
 std::atomic<int> HttpConn::userCount;
HttpConn::HttpConn(){
     fd_=-1;
     addr_={0};
     isClose_=true;
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
       readBuff_.RetrieveAll();
       writeBuff_.RetrieveAll();
       isClose_=false;
       LOG_INFO("Client[%d](%s:%d) in,usercout:%d",fd_,GetIP(),addr_.sin_port,(int)userCount);
}


void HttpConn::Close(){
    if(isClose_==false){
        response_.UnmapFile();
        isClose_=true;
        userCount--;
        ::close(fd_);
         LOG_INFO("Client[%d] quit!",GetFd());
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
    
bool HttpConn::process(){
     request_.Init();
     if(readBuff_.ReadableBytes()<=0){
        return false;
     }
     else if (request_.parse(readBuff_)) {
            LOG_DEBUG("%s",request_.getpath().c_str());
            response_.Init(srcDir,request_.getpath(),request_.IsKeepAlive(),200);
     }
     else {
           response_.Init(srcDir,request_.getpath(),false,400);
     }
     response_.MakeResponse(writeBuff_);
     //read接受请求报文，write写响应报文
     //将报文写入write——buffer，file在iov_[1]
     iov_[0].iov_base=const_cast<char*>(writeBuff_.Peek()) ;
     iov_[0].iov_len=writeBuff_.ReadableBytes();
     iovCnt_=1;
     if(response_.getFile()&&response_.getFileLen()>0){
          iov_[1].iov_base=response_.getFile();
          iov_[1].iov_len=response_.getFileLen();
          iovCnt_++;
     }
      LOG_DEBUG("filesize:%d, %d  to %d", response_.getFile() , iovCnt_, ToWriteBytes());
    return true;
}
ssize_t HttpConn::read(int* saveErrno){
    ssize_t len=0;
    do{
        len=readBuff_.ReadFd(fd_,saveErrno);
        if(len<0){
            break;
        }
    }while(isET);
    return len;
}
//将iov的0的writerbuffer和1的file
//通过iov写入fd
ssize_t HttpConn::write(int* saveErrno){
     ssize_t len=-1;
     do {
        len=writev(fd_, iov_,iovCnt_);
        if (len<0) {
           *saveErrno=errno;
            break;
        }
        if(iov_[0].iov_len+iov_[1].iov_len==0){
            break;
        }
        else if(static_cast<size_t>(len)>iov_[0].iov_len){
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
     }while (isET||ToWriteBytes()>10240);
     return len;
}