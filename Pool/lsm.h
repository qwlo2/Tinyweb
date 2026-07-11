#pragma once

#include "buffer.h"

#include <vector>
namespace LSM {
     struct lsm_result{
         std::vector<std::string> result;
         int size{-1};
     } ;
    typedef  std::string lsm_row ;
     class lsm;
}
class lsm{
    private:
     int lsmfd{-1};
    
    public:
    lsm();
    lsm(lsm* sql){
        lsmfd=sql->getlsmfd();
    }
    ~lsm();
    //创立sock
    static lsm* lsm_init();
    //发起连接
    static int lsm_real_connect(lsm* l,const char * ip,int& port );
     //发送sql查询语句
   static int lsm_quary(lsm* sql,const std::vector<std::string>& order);
    //更改语句
    static int lsm_put(lsm* sql,const std::vector<std::string>& order);
    //获取结果集
    static int lsm_result_store(lsm* sql,LSM::lsm_result& result);
    //释放结果集
    static bool lsm_result_free(LSM::lsm_result& result);
    //获取一行结果集
    static LSM::lsm_row lsm_fecth_row(LSM::lsm_result& result) ;
    //发过去后，就是key+field，因为key唯一
    std::string Respcode(const std::vector<std::string>& order);
    std::vector<std::string> Respencoding(Buffer& buf);

   static void connclose(lsm* & l);
     void setlsmfd(int fd){
        lsmfd=fd;
     }
     int  getlsmfd(){
         return  lsmfd;
     }
};