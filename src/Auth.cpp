#include "Auth.h"
#include "log.h"
#include "lsm.h"
#include "lsmconnpool.h"
#include "sqlconnpool.h"
#include "webserver.h"
#include <argon2.h>
#include <array>
#include <fstream>
#include <mysql/mysql.h>


//生成salt
bool Auth::FillRandomBytes(unsigned char* data, size_t len) {
    //从"/dev/urandom"随机读取len个作为salt
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if(!urandom) {
        return false;
    }
    //i读写，从文件读写入data
    //尝试从流中读取恰好 len 个字节，写到 data 指向的内存中。
    urandom.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(len));
    //用于判断流当前是否没有错误状态
    return urandom.good();
}
//salt和password通过Argon2id加密密码
bool Auth::HashPasswordArgon2id(const std::string& password, std::string& encoded) {
    std::array<unsigned char, ARGON2_SALT_LEN> salt{};
    if(!FillRandomBytes(salt.data(), salt.size())) {
        LOG_ERROR("Generate Argon2id salt failed");
        return false;
    }

    std::array<char, ARGON2_ENCODED_LEN> out{};
    int rc = argon2id_hash_encoded(ARGON2_TIME_COST, ARGON2_MEMORY_COST,
                                   ARGON2_PARALLELISM,
                                   password.data(), password.size(),
                                   salt.data(), salt.size(), ARGON2_HASH_LEN,
                                   out.data(), out.size());
    if(rc != ARGON2_OK) {
        LOG_ERROR("Argon2id hash failed: %d", rc);
        return false;
    }
    encoded.assign(out.data());
    // 这个字符串中已经包含：
    // 算法类型：argon2id
    // Argon2 版本
    // 内存成本
    // 时间成本
    // 并行度
    // 盐值
    // 最终哈希值
    return true;
}
 bool Auth::Auth_ar_and_SqlQuary(){
    int ret=0;
        if (islogin) {
             if (!SqlQuary()) {
                   return false;
             }
            return ar_hash_and_versity();
        }
             if (!ar_hash_and_versity()) {
                  return false;
             }
             return  SqlQuary();
        
 }
//不用salt，会自动从encoded中解析，再把salt带入，看是否相同
//数据库里面存储的是加密过后的，但不是直接将password用HashPasswordArgon2id加密然后比较
//而是调用argon2id_verify解析加密的encoded，从提取参数，在加密进行比较
bool Auth::VerifyPasswordArgon2id(const std::string& encoded, const std::string& password) {
    //它会寻找起始位置不超过 pos 的最后一次匹配。、
    //因此，用来检测开头，satrt——with
    if(encoded.rfind("$argon2id$", 0) != 0) {
        return false;
    }
    return argon2id_verify(encoded.c_str(), password.data(), password.size()) == ARGON2_OK;
}

bool Auth::ar_hash_and_versity(){
     if (islogin) {
         return  VerifyPasswordArgon2id(ar_hash_pwd,password);
     }
    return  HashPasswordArgon2id(password,ar_hash_pwd);
}
bool Auth::SqlQuary(){
       if (WebServer::db=="LSM") {
           return  quary_lsm();
        }else {
           return  quary_mysql();
        }
 }
 bool Auth::quary_mysql(){
      if(username.empty() || password.empty()){
        return false;
    }

    auto sql = SqlConnPool::Instance()->GetConn();
    if(!sql){
        return false;
    }
    //绑定string
   auto bind_string = [](MYSQL_BIND& bind, const std::string& value,
                          unsigned long& len) {
        std::memset(&bind, 0, sizeof(bind));
        len = static_cast<unsigned long>(value.size());
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = const_cast<char*>(value.data());
        bind.buffer_length = len;
        bind.length = &len;
    };
   //预处理语句初始化
    auto prepare_stmt = [&](const char* query) -> MYSQL_STMT* {
        MYSQL_STMT* stmt = mysql_stmt_init(sql.get());
        if(!stmt){
            return nullptr;
        }
        if(mysql_stmt_prepare(stmt, query, static_cast<unsigned long>(std::strlen(query)))){
            mysql_stmt_close(stmt);
            return nullptr;
        }
        return stmt;
    };
    //登录
     if(islogin){
        const char* query = "SELECT password FROM user WHERE username=? LIMIT 1";
        MYSQL_STMT* stmt = prepare_stmt(query);
        if(!stmt){
            return false;
        }

        MYSQL_BIND params[1];
        unsigned long name_len = 0;
        bind_string(params[0], username, name_len);

      //  std::string stored_hash;
        if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
            //缓冲区
            char password_buf[ARGON2_ENCODED_LEN] = {0};
            //这里不用bind_string是因为password_buf已经是char*
            unsigned long password_len = 0;
            MYSQL_BIND result[1];
            std::memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_STRING;
            result[0].buffer = password_buf;
            result[0].buffer_length = sizeof(password_buf) - 1;
            //返回的实际大小
            result[0].length = &password_len;

            if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
                //实际的大小小于缓冲区大小
                if(password_len < sizeof(password_buf)){
                   ar_hash_pwd.assign(password_buf, password_len);
                }
            }
        }
        mysql_stmt_close(stmt);
      //  ar_hash_pwd
      //这里只需判断查询是否成功
        return ! ar_hash_pwd.empty() ;
    }
    //注册
    
    //注册的加密从authuser中获取
    // std::string encoded_hash;
    // if(!HashPasswordArgon2id(pwd, encoded_hash)){
    //     return false;
    // }

    const char* query = "INSERT INTO user(username, password) VALUES(?, ?)";
    MYSQL_STMT* stmt = prepare_stmt(query);
    if(!stmt){
        return false;
    }

    MYSQL_BIND params[2];
    unsigned long name_len = 0;
    unsigned long hash_len = 0;
    bind_string(params[0],username, name_len);
    bind_string(params[1], ar_hash_pwd, hash_len);

    bool ok = !mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    //返回注册是否成功
    return ok;
 }
 bool Auth::quary_lsm(){
     if (username.empty() || password.empty()) {
        return false;
      }
        auto sql=lsmconnpool::Instance()->GetConn();
        if (!sql) {
           return false;
        }
        if (islogin) {
            bool ret=lsm::lsm_quary(sql.get(),{"hget","user",username});
            if (!ret) {
                LOG_ERROR("lsm quary error");
                return false;
            }
            LSM::lsm_result result;

            ret=lsm::lsm_result_store(sql.get(),result);
             if (!ret) {
                LOG_ERROR("lsm rece  error");
                return false;
            }
           // auto ans=std::move(lsm::lsm_fecth_row(result));
            //失败返回$-1，成功返回$n hashpassword
            if (lsm::lsm_fecth_row(result)=="$-1") {
                   return false;
            }
            ar_hash_pwd=std::move(lsm::lsm_fecth_row(result));
            // if (!VerifyPasswordArgon2id(ans,pwd)) {
            //     return false;
            // }
            return  !ar_hash_pwd.empty();
        }else {
            // std::string encoded;
            //   bool ret= HashPasswordArgon2id(pwd,encoded);
            //   if (!ret) {
            //     LOG_ERROR(" HashPasswordArgon2id error");
            //      return false;
            //   } 
        bool  ret =lsm::lsm_put(sql.get(),{"HSETNX","user",username,ar_hash_pwd});
            if (!ret) {
                LOG_ERROR("lsm quary error");
                return false;
            }
            LSM::lsm_result result;
            ret=lsm::lsm_result_store(sql.get(),result);
            if (!ret) {
               LOG_ERROR("lsm rece  error");
                return false;
            }
            auto ans=std::move(lsm::lsm_fecth_row(result));
            if (ans!=":1") {
                LOG_DEBUG("register error")
                return false;
            }
        }
        return true;
 }
 void Auth::init(){
         username={};
         password={};
        ar_hash_pwd={};
        islogin=false;
    }
    void Auth::setUsername(std::string& name){
          username=name;
    }
    void Auth::setPasaword(std::string& word){
         password=word;
    }
    void Auth::setAr_hash_pwd(std::string& pwd){
         ar_hash_pwd=pwd;
    }
    void Auth::setIslogin(bool& islogin_){
         islogin=islogin_;
    }
     std::string& Auth::getUsername(){
        return   username;
    }
    std::string& Auth::getPasaword(){
        return password;
    }
   std::string& Auth::getAr_hash_pwd(){
        return ar_hash_pwd;
    }
    bool& Auth::getIslogin(){
       return  islogin;
    }