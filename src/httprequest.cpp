#include "httprequest.h"
#include <algorithm>
//#include <iostream>
#include <cstring>
#include <memory>
#include <mysql/field_types.h>
#include <mysql/mysql.h>
#include <string>
#include <regex>
#include "log.h"
#include "sqlconnpool.h"

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
}
bool HttpRequest::parse(Buffer& buff){
    const char CRLF[]="\r\n";
    if(buff.ReadableBytes()<=0){
        return false;
    }
    while (buff.ReadableBytes()&&state_!=PARSE_STATE::FINISH) {//成功返回第一个迭代器/指针，失败end（）
         const  char* lineend=std::search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
         std::string line(buff.Peek(),lineend);
        switch (state_) {
           case  REQUEST_LINE:
                 if(!ParseRequestLine_(line)){
                    return false;
                 }
                  ParsePath_();
                  break;
            case HEADERS:
                 //会循坏解析，因为直到正则不匹配状态才改变
                 ParseHeader_(line);
                 if(buff.ReadableBytes()<=4) {
                    state_ = FINISH;
                }//判断空行
                 break;
            case BODY:
                 ParseBody_(line);
                 break;
            default:
                 break;
            }
            //请求体后没有/r/n
          if (lineend==buff.BeginWrite()) {
            if(method_=="POST"&&state_==FINISH){//requst只有post、put、patch有body，这里只考虑post
              buff.RetrieveUntil(lineend);
            }
               break;
          }
          buff.RetrieveUntil(lineend+2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
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
    if(header_.count("Connection")){
        return header_.find("Connection")->second=="keep-alive"&&version_=="1.1";
    }
    return false;
}
bool HttpRequest::ParseRequestLine_(const std::string& line){//
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");//regex，patten代表匹配规则
    std::smatch submatch;//收集后的容器
    if(std::regex_match(line,submatch,patten)){//match待匹配，容器，规则,核对规则成功才向容器填充
        method_=submatch[1];
        path_=submatch[2];
        version_=submatch[3];
        state_=PARSE_STATE::HEADERS;
        return  true;
    }
    LOG_ERROR("RequestLine error");
    return false;
}
void HttpRequest::ParseHeader_(const std::string& line){
         std::regex patten("^([^ ]*): ?(.*)$");
         std::smatch submatch;
         if(std::regex_match(line,submatch,patten)){
             header_.emplace(submatch[1], submatch[2]);
         }
         else {
             state_=PARSE_STATE::BODY;
         }
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
              if(UserVerify(post_["username"],post_["password"],islogin)){
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
    ParsePost_();
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
        }//&，=作为数据出现才转换（密码为123=456），作为分隔符不转换
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
   
 bool HttpRequest::UserVerify(const std::string& name, const std::string& pwd, bool isLogin){      
    if(name.empty() || pwd.empty()){
        return false;
    }

    auto sql = SqlConnPool::Instance()->GetConn();
    if(!sql){
        return false;
    }

    constexpr const char* PASSWORD_SALT = "TingyWebServer_Password_Salt_v1";
    const std::string salt(PASSWORD_SALT);
    //len不能绑定临时值，因此要传一个len
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
        const char* query =
            "SELECT 1 FROM user "
            "WHERE username=? AND password=SHA2(CONCAT(?, ?), 256) "
            "LIMIT 1";
        MYSQL_STMT* stmt = prepare_stmt(query);
        if(!stmt){
            return false;
        }
        //SHA2是hash加密算法，256代表结果是256bit（32B），通常用16进制表示，即64位，
        //256可以改为224 256 384 512 0，256最常用
        //不过可能被gpu大量破解，优先 Argon2id / bcrypt / scrypt / PBKDF2
        //一般要加盐即salt，因为相同的string，sha2后也相同
        //加盐让相同密码产生不同结果，并使批量预计算攻击失效
        //应该采取存储salt，然后每个salt不同，登录时通过查询salt+密码进行比较
        MYSQL_BIND params[3];
        unsigned long name_len = 0;
        unsigned long pwd_len = 0;
        unsigned long salt_len = 0;
        bind_string(params[0], name, name_len);
        bind_string(params[1], pwd, pwd_len);
        bind_string(params[2], salt, salt_len);

        bool ok = false;
        if(!mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt)){
            int marker = 0;
            MYSQL_BIND result[1];
            std::memset(result, 0, sizeof(result));
            result[0].buffer_type = MYSQL_TYPE_LONG;
            result[0].buffer = &marker;
            if(!mysql_stmt_bind_result(stmt, result) && mysql_stmt_fetch(stmt) == 0){
                ok = true;
            }
        }

        mysql_stmt_close(stmt);
        return ok;
    }
    //注册
    // if(user_exists()){
    //     return false;
    // }

    const char* query =
        "INSERT INTO user(username, password) "
        "VALUES(?, SHA2(CONCAT(?, ?), 256))";
    MYSQL_STMT* stmt = prepare_stmt(query);
    if(!stmt){
        return false;
    }

    MYSQL_BIND params[3];
    unsigned long name_len = 0;
    unsigned long pwd_len = 0;
    unsigned long salt_len = 0;
    bind_string(params[0], name, name_len);
    bind_string(params[1], pwd, pwd_len);
    bind_string(params[2], salt, salt_len);

    bool ok = !mysql_stmt_bind_param(stmt, params) && !mysql_stmt_execute(stmt);
    mysql_stmt_close(stmt);
    return ok;
 }

 int  HttpRequest::ConverHex(char ch){
         if(ch>='a'&&ch<='f')   return  ch-'a';
         if(ch>='A'&&ch<='F')   return  ch-'A';
         return  ch-'0';
 }