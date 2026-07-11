#include "lsm.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>


//仿照mysql的连接写
lsm::lsm()=default;
lsm::~lsm(){};
lsm *lsm::lsm_init() {
   
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if (fd<0) {
       return nullptr;
    }
   lsm* l=new lsm();
   l->setlsmfd(fd);
   return  l;
}
// 发起连接
int lsm::lsm_real_connect(lsm* l ,const char * ip,int& port ) {
     sockaddr_in sa;
     sa.sin_port=htons(port);
     sa.sin_family=AF_INET;
     inet_pton(AF_INET,ip,&sa.sin_addr);
     socklen_t len=sizeof(sa);
    // int ret=connect(l->getlsmfd(), (sockaddr *)&sa,  len);
     return connect(l->getlsmfd(), (sockaddr *)&sa,  len);
}
// 发送sql查询语句
int lsm::lsm_quary(lsm *sql, const std::vector<std::string> &order) {
    auto tmp=std::move(sql->Respcode(order));
     auto data=tmp.data();
     int left=0;
    do {
      int ret = write(sql->getlsmfd(), tmp.data()+left, tmp.size()-left);
      //只有当left==tmp.size()才成功，其他缓冲区满都失败
      if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)&&(left==0||left!=tmp.size())) {
         //left大于0是判断不是第一次发送就失败
        return false;
      } else if (ret > 0) {
           left+=ret;
      }else {
         return false;
      }
    }while (left!=tmp.size());
    return  true;
}
//更改语句
int lsm::lsm_put(lsm *sql, const std::vector<std::string> &order) {
   auto tmp=std::move(sql->Respcode(order));
   auto data=tmp.data();
   int left=0;
    do {
      int ret = write(sql->getlsmfd(), tmp.data()+left, tmp.size()-left);
      if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)&&(left==0||left!=tmp.size())) {
        return false;
      } else if (ret > 0) {
           left+=ret;
      }else {
         return false;
      }
    }while (left!=tmp.size());
   
    return  true;
}
// 获取结果集
int lsm::lsm_result_store(lsm* sql,LSM::lsm_result& result) {
      Buffer buf;
      int saveErrno=0;
      int ret=buf.ReadFd(sql->getlsmfd(),&saveErrno);
      if (ret<0&&(saveErrno!=EAGAIN||saveErrno!=EWOULDBLOCK)) {
              return false;
      }else if(ret==0){
         return false;
      }
     
       result.result=std::move(sql->Respencoding(buf));   
       return  true;
}
// 释放结果集
bool lsm::lsm_result_free(LSM::lsm_result& result) {
        result.result={};
        result.size=-1;
        return true;
}
// 获取一行结果集
LSM::lsm_row lsm::lsm_fecth_row(LSM::lsm_result& result){
      ++result.size;
        if (result.size<result.result.size()&&!result.result.empty()) {
             return  result.result[result.size];
        };
        return {};
}
// 发过去后，就是key+field，因为key唯一
std::string lsm::Respcode(const std::vector<std::string> &order) {
          std::string ans;
          ans+="*"+std::to_string(order.size())+"\r\n";
          for (auto& it :order) {
               ans+="$"+std::to_string(it.size())+"\r\n"+it+"\r\n";
          }
          return  ans;
}
std::vector<std::string> lsm::Respencoding(Buffer& buf) {
       std::vector<std::string> ans;
       const char cltf[]="\r\n";
       do{
         //返回pos
         const char* pos=std::search(buf.Peek(),buf.BeginWriteConst(),cltf,cltf+2);
         ans.emplace_back(std::string(buf.Peek(),pos));
         //由于返回编码不统一，只能根据具体的需要判断需要哪些，这里不能丢弃
         buf.Retrieve(pos-buf.Peek()+2);
       }while(buf.ReadableBytes());
       return ans;
}

void lsm::connclose(lsm* &l) {
     if (l->getlsmfd()>0) {
        close(l->getlsmfd());
     }
}
