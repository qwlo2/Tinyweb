#include "session.h"
#include "log.h"
#include <cassert>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstring>
#include "sha256.h"
#include "sqlconnpool.h"
#include <hiredis/hiredis.h>
#include <hiredis/read.h>
#include <mysql/field_types.h>
#include <mysql/mysql.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <sys/random.h>
#include <utility>
std::string Session::getUser_id(const std::string& username){
    auto sql = SqlConnPool::Instance()->GetConn();
    if(!sql){
        return {};
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
            return {};
        }
        if(mysql_stmt_prepare(stmt, query, static_cast<unsigned long>(std::strlen(query)))){
            mysql_stmt_close(stmt);
            return nullptr;
        }
        return stmt;
    };
     const char* query = "SELECT user_id FROM user WHERE username=? LIMIT 1";
        MYSQL_STMT* stmt = prepare_stmt(query);
        if(!stmt){
            return 0;
        }

        MYSQL_BIND params[1];
        unsigned long name_len = 0;
        //这里的绑定时执行运行预处理语句的string
        bind_string(params[0], username, name_len);
        
         std::string ans;
      //  std::string stored_hash;
        if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
            //缓冲区
            //限制姓名的大小在64字节
            char password_buf[64] = {0};
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
                   ans.assign(password_buf, password_len);
                }
            }
        }
        mysql_stmt_close(stmt);
       return ans;
}
std::optional<std::string> Session::gettoken( const std::string& uesename){
     auto userid=std::move(getUser_id(uesename));
     char random_bytes[32];
     memset(random_bytes,0,32);
     size_t offset=0,n=0;
     //从linux提取密码学安全的256位随机bit
     while (offset < 32) {
           n=getrandom(
             random_bytes+offset,//从哪里开始
            32-offset//大小
            ,0);

            if (n>0) {
               offset+=n;
               continue;
            }
            //表示系统中断
           if (n == -1 && errno == EINTR) {
              continue;
           }
        LOG_ERROR("session random_bytes creates error");
        return nullptr;
     }
     //采用base64URL编码，相比base63最后2位从+=变为-_，防止在http报文中出错
      static constexpr char TABLE[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";
     //将每字节8位随机的randow，分成每6位对于应该字节，在根据这6位字节所代表的大小在table中选择字符组成cookies
     std::string cookies;
     cookies.reserve(43);//42*6<32*8
     n=0;
     while (n+3<32) {
        //3*8=4*6刚好一次四字节，unsigned int才是4字节
         unsigned int i=static_cast<unsigned int>(random_bytes[n])<<16|
                        static_cast<unsigned int>(random_bytes[n+1])<<8|
                        static_cast<unsigned int>(random_bytes[n+2]);
                //0x3f保证后6bit为1
                cookies.push_back(TABLE[(i>>18)&0x3f]);
                cookies.push_back(TABLE[(i>>12)&0x3f]);
                cookies.push_back(TABLE[(i>>6)&0x3f]);
                cookies.push_back(TABLE[i&0x3f]);
              
                n+=3;
     }
   //处理最后2字节，32%/=2
    if (n+ 2 ==32) {
        const unsigned int value =
            (static_cast<unsigned int>(random_bytes[n]) << 16) |
            (static_cast<unsigned int>(random_bytes[n + 1]) << 8);

        cookies.push_back(TABLE[(value >> 18) & 0x3F]);
        cookies.push_back(TABLE[(value >> 12) & 0x3F]);
        //后2为补0
        cookies.push_back(TABLE[(value >> 6) & 0x3F]);
    }
    auto redis_sql=std::move(GetConn());
    const std::string key ="session:" + sha256_hex(cookies);
    const std::string ttl =std::to_string(1800);
    auto reply=static_cast<redisReply*>( 
         redisCommand(
            redis_sql.get(), 
             "SET %b %b EX %s NX",
            key.data(),key.size(),
            userid.data(),userid.size(),
            ttl.c_str() )
    );
    //只有%b,%s，%b代表地址和长度，
    if (reply==nullptr) {
         LOG_ERROR("redis creates error");
         freeReplyObject(reply);
        return  nullptr;
    }
    if (reply->type!=REDIS_REPLY_STATUS||std::string(reply->str,reply->len)!="OK") {
     LOG_ERROR("redis creates error");
     freeReplyObject(reply);
        return  nullptr;
    }
    freeReplyObject(reply);
    return cookies;
}
bool Session::versityToken(const std::string& cookie,size_t& user_id){
       auto redis_sql=std::move(GetConn());
       std::string token_hash ="session:"+sha256_hex(cookie);    // 作为 Redis Key
       std::string timeout("1800");
       auto reply=static_cast<redisReply*>(
          redisCommand(
            redis_sql.get(),
            "GETEX %b EX %s",
            token_hash.data(),
            token_hash.size(),
            timeout.data()
        )
       );
      // 表示连接、网络或协议错误，不是 Session 过期
      //应返回或转化为：HTTP/1.1 503 Service Unavailable
       if (reply==nullptr) {
        freeReplyObject(reply);
          return false;
       }
       //REDIS_REPLY_NIL代表失败
        if (reply->type == REDIS_REPLY_NIL) {
        // Key 不存在、已被删除或者已经过期
        freeReplyObject(reply);
         return false;
    } else if (reply->type == REDIS_REPLY_STRING) {
       // std::uint64_t user_id = 0;

        const char* begin = reply->str;
        const char* end = reply->str + reply->len;

        const auto [ptr, ec] =
            std::from_chars(begin, end, user_id);

        if (ec == std::errc{} && ptr == end && user_id != 0) {
            freeReplyObject(reply);
             return true;
        }
    }
    freeReplyObject(reply);
       return false;
}
bool Session::deleteToken(const std::string& token) {
    auto redis = GetConn();
    if (!redis) {
        return false;
    }

    const std::string key = "session:" + sha256_hex(token);
    auto reply = static_cast<redisReply*>(redisCommand(
        redis.get(),
        "DEL %b",
        key.data(),
        key.size()
    ));
    if (!reply) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER &&
        (reply->integer == 0 || reply->integer == 1);
    freeReplyObject(reply);
    return success;
}
 Session*  Session::Intense(){
     static Session session_;
     return  &session_;
 }

 Session::Session(){
    useCount_=0;
    freeCount_=0;
 }
    Session::~Session(){
        ClosePool();
    }


 std::shared_ptr<redisContext> Session::GetConn(){
           if (!is_inited) {
               return nullptr;
           }
            std::shared_ptr<redisContext> sql_conn;
            redisContext* sql=nullptr;

                sem_wait(&semId_);
            std::lock_guard<std::mutex> lock(mutex_);
           

            sql=connQue_.front();
            connQue_.pop();
            ++useCount_;
           --freeCount_;
             sql_conn.reset(sql,[this](redisContext* sql){
                  FreeConn(sql);
             });
            return sql_conn;
    }
    void Session::FreeConn(redisContext* conn){
           std::lock_guard<std::mutex> lock(mutex_);
           connQue_.emplace(conn);
           --useCount_;
           ++freeCount_;
           sem_post(&semId_);
    }
    int Session::GetFreeConnCount(){
        std::lock_guard<std::mutex>  lock(mutex_);
         return  freeCount_;
    }
    void Session::ClosePool(){
       
        std::lock_guard<std::mutex> lock(mutex_);
        while (!connQue_.empty()) {
            auto item=connQue_.front();
            connQue_.pop();
            redisFree(item);
        }
        sem_destroy(&semId_);
    }
    void Session::Init(const char* host, int port,int connSize){
              assert(connSize > 0);
              MAX_CONN_=connSize;
            sem_init(&semId_,0,MAX_CONN_);
             for(int i=0;i<connSize;i++)
             {
                  redisContext* sql=nullptr;
            //       unsigned int timeout = 3;
            //   mysql_options(sql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
                  sql=redisConnect(host, port);
                  if(!sql || sql->err){
                    LOG_ERROR("redis connect error: %s", sql ? sql->errstr : "allocation failed");
                    if (sql) {
                        redisFree(sql);
                    }
                    return;
                  }
                 // std::lock_guard<std::mutex> lock(mutex_);
                  connQue_.push(sql);
             }
            is_inited=true;
              LOG_INFO("Connecting Redis: host=%s, port=%d", host, port);
            
            //0代表线程间使用，1代表进程间使用
            //MAX_CONN_代表
            // 信号量值大于 0：减 1，然后继续执行；
            // 信号量值等于 0：当前线程阻塞，等待其他线程增加信号量。

    }
