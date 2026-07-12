#include "httprequest.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <fstream>
#include <argon2.h>
//#include <iostream>
#include <cstring>
#include <memory>
#include <mysql/field_types.h>
#include <mysql/mysql.h>
#include <string>
#include <regex>
#include <utility>
#include "log.h"
#include "lsmconnpool.h"
#include "sqlconnpool.h"
#include "lsm.h"
#include "webserver.h"

namespace {
constexpr uint32_t ARGON2_TIME_COST = 2;
constexpr uint32_t ARGON2_MEMORY_COST = 1 << 15; // KiB, 32 MiB
constexpr uint32_t ARGON2_PARALLELISM = 1;
constexpr size_t ARGON2_SALT_LEN = 16;
constexpr size_t ARGON2_HASH_LEN = 32;
constexpr size_t ARGON2_ENCODED_LEN = 256;
//生成salt
bool FillRandomBytes(unsigned char* data, size_t len) {
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
bool HashPasswordArgon2id(const std::string& password, std::string& encoded) {
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
//不用salt，会自动从encoded中解析，再把salt带入，看是否相同
//数据库里面存储的是加密过后的，但不是直接将password用HashPasswordArgon2id加密然后比较
//而是调用argon2id_verify解析加密的encoded，从提取参数，在加密进行比较
bool VerifyPasswordArgon2id(const std::string& encoded, const std::string& password) {
    //它会寻找起始位置不超过 pos 的最后一次匹配。、
    //因此，用来检测开头，satrt——with
    if(encoded.rfind("$argon2id$", 0) != 0) {
        return false;
    }
    return argon2id_verify(encoded.c_str(), password.data(), password.size()) == ARGON2_OK;
}
}

const std::unordered_map<std::string,int >   HttpRequest::DEFAULT_HTML_TAG{
         {"/register.html",0} ,{"/login.html",1}
};
 
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "register.html","login.html","index.html","welcome.html",
      "video.html" ,"picture.html"
};
void HttpRequest::Init(){
    state_=PARSE_STATE::REQUEST_LINE;
    method_.clear();
    path_.clear();
    version_.clear();
    body_.clear();
    header_.clear();
    post_.clear();
    contentLength_=0;
    headerBytes_=0;
    headerCount_=0;
    hasContentLength_=false;

}

HttpRequest::ParseResult HttpRequest::parse(Buffer& buff){
    const char CRLF[]="\r\n";
    while (true) {
        if(state_==PARSE_STATE::REQUEST_LINE){
            const char* lineend=std::search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
            //如果第一个/r/n就时read的最后，则是太大或者不完整
            //在判断后没有read，在报文完整时才读取
            //后续则根据stata，选择性跳过行头体，con对象持有requse和response
            if(lineend==buff.BeginWriteConst()){
                return buff.ReadableBytes()>MAX_REQUEST_LINE_SIZE
                    ? ParseResult::PayloadTooLarge
                    : ParseResult::Incomplete;
            }
            //请求行长度
            const size_t lineLen = static_cast<size_t>(lineend-buff.Peek());
            if(lineLen>MAX_REQUEST_LINE_SIZE){
                return ParseResult::PayloadTooLarge;
            }
            std::string line(buff.Peek(),lineLen);
            auto ret=ParseRequestLine_(line);
            buff.RetrieveUntil(lineend+2);
            if(ret!=ParseResult::Complete){
                return ret;
            }
            ParsePath_();
            continue;
        }

        if(state_==PARSE_STATE::HEADERS){
            const char* lineend=std::search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
            
            // headerBytes_=lineLen;
            //当为/r/n/r/n时，最后一个/r/n和peek重合不是beginwrite
            //当不全时，最后一个才是beginweite
            if(lineend==buff.BeginWriteConst()){
                return headerBytes_+buff.ReadableBytes()>MAX_HEADER_TOTAL_SIZE
                    ? ParseResult::PayloadTooLarge
                    : ParseResult::Incomplete;
            }
            //从第一个开始循坏加，直到完成或处里body
            const size_t lineLen = static_cast<size_t>(lineend-buff.Peek());
            const size_t consumedLen = lineLen+2;
            if(headerBytes_+consumedLen>MAX_HEADER_TOTAL_SIZE){
                return ParseResult::PayloadTooLarge;
            }
            //最后一个/r/n和peek重合，因此为empty
            std::string line(buff.Peek(),lineLen);
            //把剩下2个去除
            buff.RetrieveUntil(lineend+2);
            headerBytes_+=consumedLen;
             //如果有body，即使超过MAX_HEADER_COUNT，也不管
             //不对，在line空时，以及解析完所有
            if(line.empty()){
                //请求头自带的contentLength_
                if(contentLength_>MAX_BODY_SIZE){
                    return ParseResult::PayloadTooLarge;
                }
                if(contentLength_>0){
                    state_=PARSE_STATE::BODY;
                    continue;
                }
                state_=PARSE_STATE::FINISH;
                LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
                return ParseResult::Complete;
            }
            
            if(++headerCount_>MAX_HEADER_COUNT){
                return ParseResult::PayloadTooLarge;
            }
            auto ret=ParseHeader_(line);
            if(ret!=ParseResult::Complete){
                return ret;
            }
            continue;
        }

        if(state_==PARSE_STATE::BODY){
            // if(contentLength_>MAX_BODY_SIZE){
            //     return ParseResult::PayloadTooLarge;
            // }
            if(buff.ReadableBytes()<contentLength_){
                return ParseResult::Incomplete;
            }
            std::string body(buff.Peek(),contentLength_);
            buff.Retrieve(contentLength_);
            ParseBody_(body);
            LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
            return ParseResult::Complete;
        }

        if(state_==PARSE_STATE::FINISH){
            return ParseResult::Complete;
        }
    }
}

std::string HttpRequest::getpath() const{
    return path_;
}
std::string& HttpRequest::getpath(){
    return path_;
}
std::string HttpRequest::getmethod() const{
    return method_;
}
std::string HttpRequest::getversion() const{
    return version_;
}
// std::string HttpRequest::GetPost(const std::string& key) const{
//           assert(key!="");
//          if(post_.count(key)){
//             return post_.find(key)->second;
//          }
//         return "";
// }
std::string HttpRequest::GetPost(const char* key) const{
    assert(key != nullptr);
    if(post_.count(key) ) {
        return post_.find(key)->second;
    }
    return "";
}

    
bool HttpRequest::IsKeepAlive() const{
    for(const auto& item:header_){
        if(ToLower_(item.first)=="connection"){
            return ToLower_(Trim_(item.second))=="keep-alive"&&version_=="1.1";
        }
    }
    return false;
}

HttpRequest::ParseResult HttpRequest::ParseRequestLine_(const std::string& line){
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");//regex，patten代表匹配规则
    std::smatch submatch;//收集后的容器
    if(std::regex_match(line,submatch,patten)){
        method_=submatch[1];
        path_=submatch[2];
        version_=submatch[3];
        if(method_.empty()||path_.empty()||version_.empty()){
            return ParseResult::BadRequest;
        }
        state_=PARSE_STATE::HEADERS;
        return ParseResult::Complete;
    }
    LOG_ERROR("RequestLine error");
    return ParseResult::BadRequest;
}

HttpRequest::ParseResult HttpRequest::ParseHeader_(const std::string& line){
    const size_t pos=line.find(':');
    if(pos==std::string::npos||pos==0){
        return ParseResult::BadRequest;
    }

    std::string key=line.substr(0,pos);
    //检测k是否标准，不能有空格和tab（其他的也不能，只是没有检测）
    //Cookie: name=qiu; theme=dark，v可以（不能有换行、NUL 和非法控制字符）
    for(char ch:key){
        if(ch==' '||ch=='\t'){
            return ParseResult::BadRequest;
        }
    }

    std::string value=Trim_(line.substr(pos+1));
    std::string lowerKey=ToLower_(key);

    if(lowerKey=="content-length"){
        //重复
        if(hasContentLength_){
            return ParseResult::BadRequest;
        }
        size_t len=0;
        if(!ParseContentLength_(value,len)){
            return ParseResult::BadRequest;
        }
        if(len>MAX_BODY_SIZE){
            return ParseResult::PayloadTooLarge;
        }
        contentLength_=len;
        hasContentLength_=true;
    }
    //分块传输，即使发送方不知道要发多少就希望边生成数据边发送时，把报文分成多个块
    else if(lowerKey=="transfer-encoding"){
        if(ToLower_(value).find("chunked")!=std::string::npos){
            return ParseResult::BadRequest;
        }
    }

    header_[key]=value;
    return ParseResult::Complete;
}

std::string HttpRequest::Trim_(const std::string& str){
    size_t begin=0;
    while(begin<str.size()&&std::isspace(static_cast<unsigned char>(str[begin]))){
        ++begin;
    }
    size_t end=str.size();
    while(end>begin&&std::isspace(static_cast<unsigned char>(str[end-1]))){
        --end;
    }
    //如“  aa  ”“，把左右的空格去掉
    return str.substr(begin,end-begin);
}
//http的k大小写不分，全部转为小写，而v有时候需要大写
std::string HttpRequest::ToLower_(std::string str){
    // std::transform(str.begin(),str.end(),str.begin(),[](unsigned char ch){
    //     return static_cast<char>(std::tolower(ch));
    // });
    // return str;
    //把所有大写变小写
    for (char& ch : str) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return str;
}

bool HttpRequest::ParseContentLength_(const std::string& value, size_t& len){
    if(value.empty()){
        return false;
    }
    size_t result=0;
    for(char ch:value){
        if(!std::isdigit(static_cast<unsigned char>(ch))){
            return false;
        }
        size_t digit=static_cast<size_t>(ch-'0');
        //std::numeric_limits<size_t>::max()，某一数据类型的最大值
        //result*10+digit<std::numeric_limits<size_t>::max()就是整个
        if(result>(std::numeric_limits<size_t>::max()-digit)/10){
            return false;
        }
        result=result*10+digit;
    }
    len=result;
    return true;
}

void HttpRequest::ParsePath_(){    
    if(path_=="/"){
        path_="/index.html";
    }
    else {
         std::string tempPath = path_ + ".html";
        for(auto &i:DEFAULT_HTML){
              if("/"+i==tempPath){  
                path_ = tempPath;
                break;
              }
        }
    }
}
void HttpRequest::ParsePost_(){
      if(method_=="POST"&&header_["Content-Type"]=="application/x-www-form-urlencoded"){
          //ParseFromUrlencoded_();
          if(DEFAULT_HTML_TAG.count(path_)){
              int tag=DEFAULT_HTML_TAG.find(path_)->second;
              LOG_DEBUG("Tag:%d",tag);
              if(tag==0||tag==1){
              bool islogin=(tag==1);
              if(WebServer::db=="MYSQL"&&UserVerify_MYSQL(post_["username"],post_["password"],islogin)){
                     path_="/welcome.html";
              }else if (WebServer::db=="LSM"&&UserVerify_LSM(post_["username"],post_["password"],WebServer::ip,WebServer::port, islogin)) {
                    path_="/welcome.html";
              }
              else {
                  path_="/error.html";
              }
            }
          }
      }
    
}
void HttpRequest::ParseBody_(const std::string& line){
    body_=line;
    // JSON、纯文本还是二进制不解析
    if(header_["Content-Type"] == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_();
    }
   // ParsePost_();
    state_=PARSE_STATE::FINISH;
    LOG_DEBUG("Body:%s,len:%d",body_.c_str(),body_.size());
}

void HttpRequest::ParseFromUrlencoded_(){
   if(body_.empty()){
    return;
   }
   std::string value,key;
   int j=0,num=0,i=0,size=body_.size();
    char ch;
   for (;i<size;i++) {
         ch=body_[i];
         switch (ch ) {
            case '+':
                     body_[i]=' ';
                     break;
            case '=':
                     key=body_.substr(j,i-j);
                     j=i+1;
                     break;
            case '&':
                     value=body_.substr(j,i-j);
                     post_.emplace(key,value);
                     j=i+1;
                     LOG_DEBUG("%s=%s",key.c_str(),value.c_str());
                     break;
            case '%':
                     num=ConverHex(body_[i+1])*16+ConverHex(body_[i+2]);
                     //int会转为char
                     body_[i]=num;
                     body_.erase(body_.begin()+i+1,body_.begin()+i+3);
                     size-=2;
                     break;
            default: 
                    break;
        }
        //特殊字符如果承担 URL 结构作用，就保留；如果只是参数值中的普通数据，就要编码，除了+
        //+在作为分割符是空格
        //&，=作为数据出现才转换（密码为123=456），作为分隔符不转换
        // if(j > i) {
        //     std::cout<<body_.substr(i,j-i)<<std::endl;
        // };
    }
        assert(j<=i);
       if(post_.count(key)==0&&j<i){
           value=body_.substr(j,i-j);
           post_.emplace(key,value);
           LOG_DEBUG("%s=%s",key.c_str(),value.c_str());
       }
         
 }
   
 bool HttpRequest::UserVerify_MYSQL(const std::string& name, const std::string& pwd, bool isLogin){
    if(name.empty() || pwd.empty()){
        return false;
    }

    auto sql = SqlConnPool::Instance()->GetConn();
    if(!sql){
        return false;
    }

    auto bind_string = [](MYSQL_BIND& bind, const std::string& value,
                          unsigned long& len) {
        std::memset(&bind, 0, sizeof(bind));
        len = static_cast<unsigned long>(value.size());
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = const_cast<char*>(value.data());
        bind.buffer_length = len;
        bind.length = &len;
    };

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

    // auto user_exists = [&]() -> bool {
    //     const char* query = "SELECT 1 FROM user WHERE username=? LIMIT 1";
    //     MYSQL_STMT* stmt = prepare_stmt(query);
    //     if(!stmt){
    //         return false;
    //     }

    //     MYSQL_BIND params[1];
    //     unsigned long name_len = 0;
    //     bind_string(params[0], name, name_len);

    //     bool exists = false;
    //     if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
    //         int marker = 0;
    //         MYSQL_BIND result[1];
    //         std::memset(result, 0, sizeof(result));
    //         result[0].buffer_type = MYSQL_TYPE_LONG;
    //         result[0].buffer = &marker;
    //         if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
    //             exists = true;
    //         }
    //     }

    //     mysql_stmt_close(stmt);
    //     return exists;
    // };

    if(isLogin){
        const char* query = "SELECT password FROM user WHERE username=? LIMIT 1";
        MYSQL_STMT* stmt = prepare_stmt(query);
        if(!stmt){
            return false;
        }

        MYSQL_BIND params[1];
        unsigned long name_len = 0;
        bind_string(params[0], name, name_len);

        std::string stored_hash;
        if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
            //缓冲区
            char password_buf[ARGON2_ENCODED_LEN] = {0};
            //
            unsigned long password_len = 0;
            MYSQL_BIND result[1];
            std::memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_STRING;
            result[0].buffer = password_buf;
            result[0].buffer_length = sizeof(password_buf) - 1;
            //返回的实际大小
            result[0].length = &password_len;

            if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
                if(password_len < sizeof(password_buf)){
                    stored_hash.assign(password_buf, password_len);
                }
            }
        }

        mysql_stmt_close(stmt);
        return !stored_hash.empty() && VerifyPasswordArgon2id(stored_hash, pwd);
    }

    // if(user_exists()){
    //     return false;
    // }

    std::string encoded_hash;
    if(!HashPasswordArgon2id(pwd, encoded_hash)){
        return false;
    }

    const char* query = "INSERT INTO user(username, password) VALUES(?, ?)";
    MYSQL_STMT* stmt = prepare_stmt(query);
    if(!stmt){
        return false;
    }

    MYSQL_BIND params[2];
    unsigned long name_len = 0;
    unsigned long hash_len = 0;
    bind_string(params[0], name, name_len);
    bind_string(params[1], encoded_hash, hash_len);

    bool ok = !mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    return ok;
 }
// bool HttpRequest::UserVerify(const std::string& name, const std::string& pwd, bool isLogin){      
//     if(name.empty() || pwd.empty()){
//         return false;
//     }

//     auto sql = SqlConnPool::Instance()->GetConn();
//     if(!sql){
//         return false;
//     }

    // constexpr const char* PASSWORD_SALT = "TingyWebServer_Password_Salt_v1";
    // const std::string salt(PASSWORD_SALT);
     //len不能绑定临时值，因此要传一个len
    // auto bind_string = [](MYSQL_BIND& bind, const std::string& value,
    //                       unsigned long& len) {
    //     std::memset(&bind, 0, sizeof(bind));
    //     len = static_cast<unsigned long>(value.size());
    //     bind.buffer_type = MYSQL_TYPE_STRING;
    //     bind.buffer = const_cast<char*>(value.data());
    //     bind.buffer_length = len;
    //     bind.length = &len;
    // };

    // auto prepare_stmt = [&](const char* query) -> MYSQL_STMT* {
    //     MYSQL_STMT* stmt = mysql_stmt_init(sql.get());
    //     if(!stmt){
    //         return nullptr;
    //     }
    //     if(mysql_stmt_prepare(stmt, query, static_cast<unsigned long>(std::strlen(query)))){
    //         mysql_stmt_close(stmt);
    //         return nullptr;
    //     }
    //     return stmt;
    // };

    // auto user_exists = [&]() -> bool {
    //     const char* query = "SELECT 1 FROM user WHERE username=? LIMIT 1";
    //     MYSQL_STMT* stmt = prepare_stmt(query);
    //     if(!stmt){
    //         return false;
    //     }

    //     MYSQL_BIND params[1];
    //     unsigned long name_len = 0;
    //     bind_string(params[0], name, name_len);

    //     bool exists = false;
    //     if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
    //         int marker = 0;
    //         MYSQL_BIND result[1];
    //         std::memset(result, 0, sizeof(result));
    //         result[0].buffer_type = MYSQL_TYPE_LONG;
    //         result[0].buffer = &marker;
    //         if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
    //             exists = true;
    //         }
    //     }

    //     mysql_stmt_close(stmt);
    //     return exists;
    // };
  
    // if(isLogin){
    //     const char* query =
    //         "SELECT 1 FROM user "
    //         "WHERE username=? AND password=SHA2(CONCAT(?, ?), 256) "
    //         "LIMIT 1";
    //     MYSQL_STMT* stmt = prepare_stmt(query);
    //     if(!stmt){
    //         return false;
    //     }
        //SHA2是hash加密算法，256代表结果是256bit（32B），通常用16进制表示，即64位，
        //256可以改为224 256 384 512 0，256最常用
        //不过可能被gpu大量破解，优先 Argon2id / bcrypt / scrypt / PBKDF2
        //一般要加盐即salt，因为相同的string，sha2后也相同
        //加盐让相同密码产生不同结果，并使批量预计算攻击失效
        //应该采取存储salt，然后每个salt不同，登录时通过查询salt+密码进行比较
    //     MYSQL_BIND params[3];
    //     unsigned long name_len = 0;
    //     unsigned long pwd_len = 0;
    //     unsigned long salt_len = 0;
    //     bind_string(params[0], name, name_len);
    //     bind_string(params[1], pwd, pwd_len);
    //     bind_string(params[2], salt, salt_len);

    //     bool ok = false;
    //     if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
    //         int marker = 0;
    //         MYSQL_BIND result[1];
    //         std::memset(result, 0, sizeof(result));
    //         result[0].buffer_type = MYSQL_TYPE_LONG;
    //         result[0].buffer = &marker;
    //         if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
    //             ok = true;
    //         }
    //     }

    //     mysql_stmt_close(stmt);
    //     return ok;
    // }
    //注册
    // if(user_exists()){
    //     return false;
    // }

//     const char* query =
//         "INSERT INTO user(username, password) "
//         "VALUES(?, SHA2(CONCAT(?, ?), 256))";
//     MYSQL_STMT* stmt = prepare_stmt(query);
//     if(!stmt){
//         return false;
//     }

//     MYSQL_BIND params[3];
//     unsigned long name_len = 0;
//     unsigned long pwd_len = 0;
//     unsigned long salt_len = 0;
//     bind_string(params[0], name, name_len);
//     bind_string(params[1], pwd, pwd_len);
//     bind_string(params[2], salt, salt_len);

//     bool ok = !mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt);
//     mysql_stmt_close(stmt);
//     return ok;
//  }
 int  HttpRequest::ConverHex(char ch){
         if(ch>='a'&&ch<='f')   return  ch-'a'+10;
         if(ch>='A'&&ch<='F')   return  ch-'A'+10;
         return  ch-'0';
 }
 bool HttpRequest::UserVerify_LSM(const std::string& name, const std::string& pwd,const char * ip,int port, bool isLogin){
   if (name.empty() || pwd.empty()) {
     return false;
   }
        auto sql=lsmconnpool::Instance()->GetConn();
        if (!sql) {
           return false;
        }
        if (isLogin) {
            bool ret=lsm::lsm_quary(sql.get(),{"hget","user",name});
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
            //失败返回$-1，成功返回$n hashpassword
            if (ans=="$-1") {
                   return false;
            }
            ans=std::move(lsm::lsm_fecth_row(result));
            if (!VerifyPasswordArgon2id(ans,pwd)) {
                return false;
            }
           
        }else {
            std::string encoded;
              bool ret= HashPasswordArgon2id(pwd,encoded);
              if (!ret) {
                LOG_ERROR(" HashPasswordArgon2id error");
                 return false;
              } 
          ret =lsm::lsm_put(sql.get(),{"HSETNX","user",name,encoded});
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