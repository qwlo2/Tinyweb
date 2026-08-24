#include "file_shared.h"
#include "log.h"
#include "session.h"
#include "sha256.h"
#include "sqlconnpool.h"
#include <charconv>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mysql/mysql.h>
#include <optional>
#include <string>
#include <sys/random.h>
#include <utility>
std::optional<std::string> File_shared::get_share_token(){

    auto random_bytes=std::move(get_code(16));
     //采用base64URL编码，相比base63最后2位从+=变为-_，防止在http报文中出错
      static constexpr char TABLE[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";
     //将每字节8位随机的randow，分成每6位对于应该字节，在根据这6位字节所代表的大小在table中选择字符组成cookies
     std::string token;
     token.reserve(22);
     size_t n=0;
     while (n+3<16) {
        //3*8=4*6刚好一次四字节，unsigned int才是4字节
         unsigned int i=static_cast<unsigned int>(random_bytes[n])<<16|
                        static_cast<unsigned int>(random_bytes[n+1])<<8|
                        static_cast<unsigned int>(random_bytes[n+2]);
                //0x3f保证后6bit为1
                token.push_back(TABLE[(i>>18)&0x3f]);
                token.push_back(TABLE[(i>>12)&0x3f]);
                token.push_back(TABLE[(i>>6)&0x3f]);
                token.push_back(TABLE[i&0x3f]);
              
                n+=3;
     }
    if (n+ 1 ==16) {
        const unsigned int value =
                      (static_cast<unsigned int>(random_bytes[n]) << 8) ;
                  token.push_back(TABLE[(value >> 10) & 0x3F]);
                  // 后2为补0
                  token.push_back(TABLE[(value >> 4) & 0x3F]);
    }
    delete [] random_bytes;
    return  token;
}
char* File_shared::get_code(int bits){
      char* random_bytes=new char[bits];
     memset(random_bytes,0,bits);
     size_t offset=0,n=0;
     //从linux提取密码学安全的256位随机bit
     while (offset < bits) {
           n=getrandom(
             random_bytes+offset,//从哪里开始
            bits-offset//大小
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

     return random_bytes;
}
  File_shared*   File_shared::Instance(){
     static File_shared share;
     return  &share;
}
bool File_shared::valid_filename(const std::string& filename){
    if (filename.empty() || filename.size() > 255)
        return false;

    if (filename == "." || filename == "..")
        return false;

    for (unsigned char c : filename)
    {
        if (c == '/' || c == '\\' || c == '\0' || c < 0x20)
            return false;
    }

    return true;
}
bool File_shared::valid_share_token(const std::string& token){
    if (token.empty() || token.size() > 32)
        return false;

    for (unsigned char c : token)
    {
        if (!(std::isalnum(c) || c == '-' || c == '_'))
            return false;
    }

    return true;
}
//会上传hascode=true/false，expire_time=n/null
std::optional<std::pair<std::string,std::string>>   File_shared::share_file(const std::string& has_code,size_t& user_id,
    const std::string& filename,const std::string& time){
           auto token=std::move(get_share_token());
           if (!token) {
              return std::nullopt;
           }
           std::string code_hah="NULL";
           if (has_code=="true") {
                 auto tmp=std::move(get_code(4));
                 static constexpr char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                 "abcdefghijklmnopqrstuvwxyz"
                                                 "0123456789-_";
                 std::string str;
                 str.reserve(22);
                 size_t n = 0;
                 while (n + 3 < 4) {
                   // 3*8=4*6刚好一次四字节，unsigned int才是4字节
                   unsigned int i = static_cast<unsigned int>(tmp[n]) << 16 |
                                    static_cast<unsigned int>(tmp[n + 1]) << 8 |
                                    static_cast<unsigned int>(tmp[n + 2]);
                   // 0x3f保证后6bit为1
                   str.push_back(TABLE[(i >> 18) & 0x3f]);
                   str.push_back(TABLE[(i >> 12) & 0x3f]);
                   str.push_back(TABLE[(i >> 6) & 0x3f]);
                   str.push_back(TABLE[i & 0x3f]);

                   n += 3;
                 }
                if (n + 1 == 4) {
                  const unsigned int value =
                      (static_cast<unsigned int>(tmp[n]) << 8) ;
                  str.push_back(TABLE[(value >> 10) & 0x3F]);
                  // 后2为补0
                  str.push_back(TABLE[(value >> 4) & 0x3F]);
                }
                code_hah.swap(str);
           }
           std::string expire_time="NULL";
           //这里可能存在sql注入的分享，因此暂时不支持自定义时间
           if (time!="null") {
              expire_time="DATE_ADD(NOW(), INTERVAL "+time+" DAY)";
           }
            auto sql=SqlConnPool::Instance()->GetConn();
           // MYSQL_STMT* pre_stmt.get()(mysql_stmt_init(sql.get()));
            std::shared_ptr<MYSQL_STMT> pre_stmt(mysql_stmt_init(sql.get()),
               [](MYSQL_STMT* stmt){
                     if(stmt){
                        mysql_stmt_close(stmt);
                     }
                  });
            const std::string token_sql = "'" + token.value() + "'";

            const std::string code_sql =
                has_code == "true" ? "'" + code_hah + "'" : "NULL";

            const std::string order =
                "INSERT INTO share "
                "(share_token, file_id, code_hash, expire_time) "
                "SELECT " +
                token_sql + ", file_id, " + code_sql +
                ", NULL "
                "FROM file "
                "WHERE user_id = " +
                std::to_string(user_id) + " AND file_name = ?";
            MYSQL_BIND bind[1]{};

            unsigned long filename_len =
                static_cast<unsigned long>(filename.size());

            bind[0].buffer_type = MYSQL_TYPE_STRING;
            bind[0].buffer = const_cast<char *>(filename.data());
            bind[0].buffer_length = filename_len;
            bind[0].length = &filename_len;

            bool file_ok = mysql_stmt_prepare(
                               pre_stmt.get(), order.data(),
                               static_cast<unsigned long>(order.size())) == 0 &&
                           mysql_stmt_bind_param(pre_stmt.get(), bind) == 0 &&
                           mysql_stmt_execute(pre_stmt.get()) == 0;
            if (!file_ok) {
              return std::nullopt;
            }
                //插入了几行，看是否存在文件
                file_ok= mysql_stmt_affected_rows(pre_stmt.get());
                if (!file_ok) {
                 //插入失败
                   return std::nullopt;
                }
             return std::make_optional(std::make_pair(token.value(),code_hah));
}
//验证码通过进入是否下载html
bool  File_shared::versity_share_token(const std::string& token,const std::string& code){
        if (!valid_share_token(token)) {
           return false;
        }
         auto sql=SqlConnPool::Instance()->GetConn();
    
          std::shared_ptr<MYSQL_STMT> pre_stmt(mysql_stmt_init(sql.get()),
               [](MYSQL_STMT* stmt){
                     if(stmt){
                        mysql_stmt_close(stmt);
                     }
                  });

            const std::string order="SELECT share_id,code_hash from share where share_token=? "
                                    " AND (expire_time IS NULL OR expire_time > NOW() );";

                MYSQL_BIND bind[1]{};

            unsigned long token_len = static_cast<unsigned long>(token.size());

            bind[0].buffer_type = MYSQL_TYPE_STRING;
            bind[0].buffer = const_cast<char *>(token.data());
            bind[0].buffer_length = token_len;
            bind[0].length = &token_len;

            bool file_ok = mysql_stmt_prepare(
                               pre_stmt.get(), order.data(),
                               static_cast<unsigned long>(order.size())) == 0 &&
                           mysql_stmt_bind_param(pre_stmt.get(), bind) == 0 &&
                           mysql_stmt_execute(pre_stmt.get()) == 0;
            if (!file_ok) {
              return false;
            }
                MYSQL_BIND res[2]{};
                
                unsigned long share_id;
                res[0].buffer_type = MYSQL_TYPE_LONGLONG;
                res[0].buffer = &share_id;
                res[0].is_unsigned = true;

                unsigned long code_len = 4;
                std::string code_hash;
                code_hash.resize(4);
                bool code_hash_is_null = false;
                
                res[1].buffer_type = MYSQL_TYPE_STRING;
                res[1].buffer = const_cast<char *>(code_hash.data());
                res[1].buffer_length = code_len;
                res[1].length = &code_len;
                res[1].is_null=&code_hash_is_null;


                file_ok=mysql_stmt_bind_result(pre_stmt.get(),res)==0&&
                   mysql_stmt_store_result(pre_stmt.get())==0&&mysql_stmt_fetch(pre_stmt.get())==0;

                 if (!file_ok) {
                    return false;
                 }
                 
                if (!code_hash_is_null&&code_hash!=code) {
                    return false;
                }
                //code是null或者code正确
                auto redis_sql=Session::Intense()->GetConn();
                //key,value
                std::string share_auth="share_auth:"+sha256_hex(token);
                std::string value=std::to_string(share_id);
                const std::string ttl =std::to_string(1800);

                auto reply = static_cast<redisReply *>(redisCommand(
                    redis_sql.get(),
                     "SET %b %b EX %s NX", 
                     share_auth.data(),
                    share_auth.size(), 
                    value.data(),
                     value.size(), 
                     ttl.data()));
                 //nullptr代表底层客户端出错
                  if (!reply) {
                     LOG_ERROR("share_auth creates error");
                     freeReplyObject(reply);
                     return  false;
                  }
                  if (reply->type != REDIS_REPLY_STATUS ||
                      std::string(reply->str, reply->len) != "OK") {
                    LOG_ERROR("share_auth creates error");
                    freeReplyObject(reply);
                    return false;
                  }
                  freeReplyObject(reply);

                  return true;
}
//验证下载时是否有效,随后进入download
bool  File_shared::versity_doenload(size_t& file_id,  std::string& auth_hash){
           size_t share_id=0;
       {
         auto redis_sql=Session::Intense()->GetConn();
          auto reply = static_cast<redisReply *>(redisCommand(
                    redis_sql.get(),
                     "GET %b  ", 
                     auth_hash.data(),
                    auth_hash.size()));
                 //nullptr代表底层客户端出错
                  // 表示连接、网络或协议错误，不是 Session 过期
                  // 应返回或转化为：HTTP/1.1 503 Service Unavailable
                  if (!reply) {
                     LOG_ERROR("share_auth select error");
                     freeReplyObject(reply);
                     return  false;
                  }
        
                  // REDIS_REPLY_NIL代表失败
                 
                  if (reply->type == REDIS_REPLY_NIL) {
                    // Key 不存在、已被删除或者已经过期
                    freeReplyObject(reply);
                    return false;
                  } else if (reply->type == REDIS_REPLY_STRING) {
                    // std::uint64_t user_id = 0;

                    const char *begin = reply->str;
                    const char *end = reply->str + reply->len;

                    const auto [ptr, ec] = std::from_chars(begin, end, share_id);

                    if (ec != std::errc{} && ptr != end && share_id == 0) {
                      freeReplyObject(reply);
                      return false;
                    }
                  }
                  freeReplyObject(reply);
        }
                 //接下来验证文件是否被取消分享
                 auto sql=SqlConnPool::Instance()->GetConn();
                  const std::string order = "SELECT share_token ,file_id from share where share_id="+std::to_string(share_id);
                    
              bool sussecc=mysql_query(sql.get(), order.c_str());
              if (sussecc) {
                  LOG_ERROR("shared cancle");
                  return false;
              }
              MYSQL_RES* res=mysql_store_result(sql.get());
              if (!res) {
               LOG_ERROR(" MYSQL_RES create error");
                  return false;
              }
               MYSQL_ROW row = mysql_fetch_row(res);
               if (!row) {
                 // SELECT 成功，但没有查询到记录
                  LOG_DEBUG("share_download fetch error")
                 mysql_free_result(res);
                 return false;
               }
                unsigned long* len=mysql_fetch_lengths(res);//len[n]每一列的长度
                    if (sha256_hex("share_auth:"+std::string(row[0],len[0]))!=auth_hash) {
                         LOG_DEBUG("share not exits");
                         return false;
                    }   
                file_id=std::stoi(std::string(row[1],len[1]));
              mysql_free_result(res);
              return true;
}

 std::string File_shared::vsersity_ShareAccess(const std::string& token){
    if (!valid_share_token(token)) {
           return  "";
        }
        auto sql=SqlConnPool::Instance()->GetConn();
      //判断code
      std::shared_ptr<MYSQL_STMT> pre_stmt(mysql_stmt_init(sql.get()),
               [](MYSQL_STMT* stmt){
                     if(stmt){
                        mysql_stmt_close(stmt);
                     }
                  });
        const std::string order="SELECT code_hash ,share_id from share where share_token=?"
                                    "  AND (expire_time IS NULL OR expire_time > NOW() );";
                 bool sussecc=mysql_query(sql.get(), order.c_str());
               MYSQL_BIND bind[1]{};

            unsigned long token_len = static_cast<unsigned long>(token.size());

            bind[0].buffer_type = MYSQL_TYPE_STRING;
            bind[0].buffer = const_cast<char *>(token.data());
            bind[0].buffer_length = token_len;
            bind[0].length = &token_len;

            bool file_ok = mysql_stmt_prepare(
                               pre_stmt.get(), order.data(),
                               static_cast<unsigned long>(order.size())) == 0 &&
                           mysql_stmt_bind_param(pre_stmt.get(), bind) == 0 &&
                           mysql_stmt_execute(pre_stmt.get()) == 0;
            if (!file_ok) {
              return "";
            }
            //加入redis，设置auth-token
                MYSQL_BIND res[2]{};
                //getrandom的字符可能无法表示，用base64url有翻译了，就是6B
                unsigned long code_len = 6;
                std::string code_hash;
                code_hash.resize(6);
                bool code_hash_is_null = false;

                res[0].buffer_type = MYSQL_TYPE_STRING;
                 res[0].buffer = const_cast<char *>(code_hash.data());
                 res[0].buffer_length = code_len;
                 res[0].length = &code_len;
                 res[0].is_null=&code_hash_is_null;
                 
                unsigned long share_id;
                res[1].buffer_type = MYSQL_TYPE_LONGLONG;
                res[1].buffer = &share_id;
                res[1].is_unsigned = true;

                file_ok=mysql_stmt_bind_result(pre_stmt.get(),res)==0&&
                   mysql_stmt_store_result(pre_stmt.get())==0&&mysql_stmt_fetch(pre_stmt.get())==0;

                 if (!file_ok) {
                    return "";
                 }
                 //会返回1，0的字符
                  //code是null或者code正确
                  if (!code_hash_is_null) {
                         return  "true";
                  }
                auto redis_sql=Session::Intense()->GetConn();
                //key,value
                std::string share_auth="share_auth:"+sha256_hex(token);
                std::string value=std::to_string(share_id);
                const std::string ttl =std::to_string(1800);

                auto reply = static_cast<redisReply *>(redisCommand(
                    redis_sql.get(),
                     "SET %b %b EX %s NX", 
                     share_auth.data(),
                    share_auth.size(), 
                    value.data(),
                     value.size(), 
                     ttl.data()));
                 //nullptr代表底层客户端出错
                  if (!reply) {
                     LOG_ERROR("share_auth creates error");
                     freeReplyObject(reply);
                     return  "";
                  }
                  if (reply->type != REDIS_REPLY_STATUS ||
                      std::string(reply->str, reply->len) != "OK") {
                    LOG_ERROR("share_auth creates error");
                    freeReplyObject(reply);
                    return "";
                  }
                  freeReplyObject(reply);
                 return  "false";
 }
