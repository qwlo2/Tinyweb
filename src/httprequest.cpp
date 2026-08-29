#include "httprequest.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <argon2.h>
//#include <iostream>
#include <cstring>
#include <mysql/field_types.h>
#include <mysql/mysql.h>
#include <optional>
#include <string>
#include <utility>
#include "download.h"
#include "log.h"

const std::unordered_map<std::string,int >   HttpRequest::DEFAULT_HTML_TAG{
         {"/register.html",0} ,{"/login.html",1}
};
 
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "register.html","login.html","index.html","welcome.html",
      "video.html" ,"picture.html","file"
};
void HttpRequest::Init(){
    state_=PARSE_STATE::REQUEST_LINE;
    method_=HttpMethod::UNKNOWN;

    path_.clear();
    version_.clear();
    body_.clear();
    header_.clear();
    post_.clear();

    contentLength_=0;
    headerBytes_=0;
    headerCount_=0;
    hasContentLength_=false;
    ready_rece_data=false;
    file_filed.clear();
    
    route_={};
    route_token_={};
}
//上传/下载不走这里
ParseResult HttpRequest::parse(Buffer& buff){
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
            ParseRoute_();//解析路由
            continue;
        }

        if(state_==PARSE_STATE::HEADERS){
            //返回值是first1, last1,的last1，当为空时时first1
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
             //在line空时，已经解析完所有
            if(line.empty()){
                //请求头自带的contentLength_
                if(contentLength_>MAX_BODY_SIZE){
                    return ParseResult::PayloadTooLarge;
                }
                if(contentLength_>0&&route_==RouteType::Upload){
                    state_=PARSE_STATE::FILR_BODY;
                    continue;
                }else if (contentLength_>0) {
                    state_=PARSE_STATE::BODY;
                    continue;
                }
                //在 Header 遇到空行、又没有 body 时,parseResult()被跳过
                state_=PARSE_STATE::FINISH;
                LOG_DEBUG("[%s], [%s], [%s]", method_, path_.c_str(), version_.c_str());
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
        //和body一样都是把信息解析完，然后上传的操作单独有函数，parepost中也把文件的原信息给file——upload类
        if (state_==PARSE_STATE::FILR_BODY) {
               const char* lineend=std::search(buff.Peek(),buff.BeginWriteConst(),CRLF,CRLF+2);
               if (lineend==buff.BeginWrite()) {
                  return  ParseResult::Incomplete;
               }
              //最后一个/r/n和peek重合，因此为empty
            std::string line(buff.Peek(),lineend-buff.Peek());
            //把剩下2个去除
            buff.RetrieveUntil(lineend+2);
            if (line.empty()||line=="--"+header_["boundary"]) {
                if (ready_rece_data) {
                    state_=PARSE_STATE::FINISH;
                    return  ParseResult::Complete;
                }
               continue;
            }
          auto ret=std::move(ParseFileBody(line));
          if(ret!=ParseResult::Complete){
                return ret;
            }
            continue;
        }
        if(state_==PARSE_STATE::BODY){
            // if(contentLength_>MAX_BODY_SIZE){
            //     return ParseResult::PayloadTooLarge;
            // }
            //普通的
            if(buff.ReadableBytes()<contentLength_){
                return ParseResult::Incomplete;
            }
            std::string body(buff.Peek(),contentLength_);
            buff.Retrieve(contentLength_);
            ParseBody_(body);
            LOG_DEBUG("[%s], [%s], [%s]", method_, path_.c_str(), version_.c_str());
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
HttpMethod HttpRequest::getmethod() const{
    return method_;
}
std::string HttpRequest::getversion() const{
    return version_;
}
//  HttpRequest::ParseResult HttpRequest::parseResult() {
//       if ( method_ == "POST" &&
//            (path_ == "/login.html" || path_ == "/register.html")) {
//           return ParseResult::NeedAuth;
//       }
//       if (method_== "POST" &&path_ == "/file") {
//               return ParseResult::Upload;
//       }
//       if (method_=="GET"&&path_.starts_with("/file")) {
//            file_filed.emplace_back(path_.substr(6));
//            if (header_.contains("range")) {
//                  file_filed.emplace_back(header_["range"]);
//            }
//             return  ParseResult::Download;
//       }
//       return  ParseResult::Complete;
// }
std::string HttpRequest::GetPost(const char* key) const{
   // assert(key != nullptr);
    if(post_.contains(key) ) {
        return post_.find(key)->second;
    }
    return "";
}
std::string HttpRequest::GetPost(const std::string& key) const{
        if(post_.contains(key) ) {
        return post_.find(key)->second;
    }
    return "";
}
    std::string HttpRequest::Getheader(const char* key) const{
   // assert(key != nullptr);
    if(header_.contains(key) ) {
        return header_.find(key)->second;
    }
    return "";
}
std::string HttpRequest::Getheader(const std::string& key) const{
        if(header_.contains(key) ) {
        return header_.find(key)->second;
    }
    return "";
}
 std::optional<std::string > HttpRequest::get_cookie(const std::string& name){
    auto pos=header_["cookie"].find(name);
    if (pos==std::string::npos) {
       return  std::nullopt;
    }
    auto tmp=std::move(header_["cookie"].substr(pos));
    auto pos1=tmp.find_first_of("=");
     auto pos2=tmp.find_first_of(";");
     if (pos1==std::string::npos) {
       return  std::nullopt;
    }
    //此时到最后一个，对于这个
    if (pos2==std::string::npos) {
       pos2 = tmp.size();
       if (pos2==pos1+1) {
          return std::nullopt;
       }
    } 
     return  tmp.substr(pos1+1,pos2-pos1-1);
    
}
 std::optional<std::string > HttpRequest::get_cookie(const char*& name){
       auto pos=header_["cookie"].find(name);
    if (pos==std::string::npos) {
       return  std::nullopt;
    }
    auto tmp=std::move(header_["cookie"].substr(pos));
    auto pos1=tmp.find_first_of("=");
     auto pos2=tmp.find_first_of(";");
       if (pos1==std::string::npos||pos2==std::string::npos) {
       return  std::nullopt;
    }
     return  tmp.substr(pos1+1,pos2-pos1-1);
 }
bool HttpRequest::IsKeepAlive() const{
     std::string connection;

    for (const auto& item : header_) {
        if (ToLower_(item.first) == "connection") {
            connection = ToLower_(Trim_(item.second));
            break;
        }
    }

    if (version_ == "1.1") {
        return connection != "close";
    }

    if (version_ == "1.0") {
        return connection == "keep-alive";
    }
    return false;
}
//下载时的GET /file/a.txt HTTP/1.1，path取出filename在download的paradoen中进行
ParseResult HttpRequest::ParseRequestLine_(const std::string& line){
   
    auto first = line.find(" ");
    if (first == std::string::npos || first == 0) {
      return ParseResult::BadRequest;
    }
    auto second = line.find(" ", first + 1);
    if (second == std::string::npos || first == 0) {
      return ParseResult::BadRequest;
    }
    if (line.find(' ', second + 1) != std::string::npos) {
    return ParseResult::BadRequest;
    }

    std::string  method=line.substr(0,first);
    if (method == "GET") {
      method_ = HttpMethod::GET;
    } else if (method == "POST") {
      method_ = HttpMethod::POST;
    } else {
      method_ = HttpMethod::UNKNOWN;
    }

    path_=line.substr(first+1,second-first-1);
    if (line.compare(second + 1, 5, "HTTP/") != 0) {
          LOG_ERROR("RequestLine error");
      return ParseResult::BadRequest;
    }
    version_ = line.substr(second + 6);
    if (method_==HttpMethod::UNKNOWN || path_.empty() || version_.empty()) {
          LOG_ERROR("RequestLine error");
      return ParseResult::BadRequest;
    }
    state_ = PARSE_STATE::HEADERS;
    return ParseResult::Complete;

}
ParseResult HttpRequest::ParseHeader_(const std::string& line){
     size_t pos=line.find(':');
    if(pos==std::string::npos||pos==0){
        return ParseResult::BadRequest;
    }
    //检测k是否标准，不能有空格和tab（其他的也不能，只是没有检测）
    //Cookie: name=qiu; theme=dark，v可以（不能有换行、NUL 和非法控制字符）
    //=时kv的左右2边可以有空格，但是k里面不能有，v可以有
    std::string key=Trim_(line.substr(0,pos));
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
    }else if(lowerKey=="transfer-encoding"){
         //分块传输，即使发送方不知道要发多少就希望边生成数据边发送时，把报文分成多个块
        if(ToLower_(value).find("chunked")!=std::string::npos){
            return ParseResult::BadRequest;
        }
    }else if (lowerKey=="content-type"&&route_==RouteType::Upload) {
       // Content-Type: multipart/form-data; boundary=----abc123
        //保持：的位置并找到;的位置
           int tmp=pos;
           pos=line.find_first_of(";");
           value=Trim_(line.substr(tmp+1,pos-tmp-1));
           header_[lowerKey]=value;
        //保存boundary
           tmp=pos;
           pos=line.find_first_of("=");

            key=Trim_(line.substr(tmp+1,pos-tmp-1));
           for (char ch : key) {
             if (ch == ' ' || ch == '\t') {
               return ParseResult::BadRequest;
             }
           }
          
           value=Trim_(line.substr(pos+1));
           lowerKey=ToLower_(key);
    }
    // if (lowerKey=="cookie") {
    //   value= value.substr(value.find_first_of("=")+1);
    // }
    header_[lowerKey]=value;
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
    else if (!DEFAULT_HTML.contains(path_)) {
        return;
    }else {
         std::string tempPath = path_ + ".html";
        for(auto &i:DEFAULT_HTML){
              if("/"+i==tempPath){  
                path_ = tempPath;
                break;
              }
        }
    }
}
// void HttpRequest::ParsePost_(){
//       if(method_=="POST"&&(path_=="register.html"||path_=="login.html")){
//           //ParseFromUrlencoded_();
//               int tag=DEFAULT_HTML_TAG.find(path_)->second;
//               LOG_DEBUG("Tag:%d",tag);
//                bool islogin_=tag == 1;
//                 authuser.setIslogin(islogin_);
//                 authuser.setUsername(post_["username"]);
//                 authuser.setPasaword(post_["password"]);
//                 path_="/error.html";
//       }else  {
//            int tag=DEFAULT_HTML_TAG.find(path_)->second;
//               LOG_DEBUG("Tag:%d",tag);
//                 file.parase_filed(file_filed);
//                 path_="/error.html";
//       }
    
// }
 void HttpRequest::paraAuth(Auth& authuser){
 if(route_==RouteType::NeedAuth){
          //ParseFromUrlencoded_();
              int tag=DEFAULT_HTML_TAG.find(path_)->second;
              LOG_DEBUG("Tag:%d",tag);
               bool islogin_=tag == 1;
                authuser.setIslogin(islogin_);
                authuser.setUsername(post_["username"]);
                authuser.setPasaword(post_["password"]);
                path_="/error.html";
      }
    if (route_==RouteType::ShareLogin) {
              LOG_DEBUG("share Login");
               bool islogin_= 1;
                authuser.setIslogin(islogin_);
                authuser.setUsername(post_["username"]);
                authuser.setPasaword(post_["password"]);
                path_="/error.html";
    }
 }
void HttpRequest::para_up_File(UploadFile& filer){
  if(route_==RouteType::Upload){
  //  int tag=DEFAULT_HTML_TAG.find(path_)->second;
             // LOG_DEBUG("upload file:%s",file_filed.);
                filer.parase_filed(file_filed);
                filer.get_boundary()=header_["boundary"];
                 LOG_DEBUG("upload file:%s",filer.get_filename().c_str());
                path_="/error.html";
  }
}
void HttpRequest::para_down_File(Download& filer){
       if(route_==RouteType::Download){
  //  int tag=DEFAULT_HTML_TAG.find(path_)->second;
             // LOG_DEBUG("upload file:%s",file_filed.);
            //  file_filed.emplace_back(path_.substr(6));
            std::string filename = path_.substr(6);
        //url的语法对一些会进行转换，会对业务数据进行转换，path不会
        for (size_t i = 0; i + 2 < filename.size(); ++i) {
            if (filename[i] == '%') {
                filename[i] = static_cast<char>(
                    ConverHex(filename[i + 1]) * 16 +
                    ConverHex(filename[i + 2])
                );
                filename.erase(i + 1, 2);
            }
        }

        file_filed.emplace_back(std::move(filename));
           if (header_.contains("range")) {
                 file_filed.emplace_back(header_["range"]);
           }
                filer.parase_filed(file_filed);
                 LOG_DEBUG("upload file:%s",filer.get_filename().c_str());
                path_="/error.html";
  }
}

void HttpRequest::ParseBody_(const std::string& line){
    body_=line;
    // JSON、纯文本还有二进制不解析
    if(header_["content-type"] == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_();
    }
   // ParsePost_();
    state_=PARSE_STATE::FINISH;
    LOG_DEBUG("Body:%s,len:%d",body_.c_str(),body_.size());
}
ParseResult HttpRequest::ParseFileBody(const std::string& line){
//resume
// ------TinyWebBoundary
// Content-Disposition: form-data; name="file"; filename="hello.txt"
// Content-Type: text/plain
//只有三种情况，boundary被我跳过了
//一是part——data，也就是resume;二是Content-Disposition: form-data; name="file"; filename="hello.txt"这种类型;
//三是Content-Type: text/plain
    auto pos=line.find(":");
    if (pos==std::string::npos) {
        //此时是part——data
        file_filed.emplace_back(line);
    }else if ( std::string tmp(ToLower_(line.substr(0,pos)));tmp=="content-type") {
        //此时是Content-Type
        file_filed.emplace_back(line.substr(0,pos));
          file_filed.emplace_back(Trim_(line.substr(pos+1)));
           ready_rece_data=true;
    }else {
       //此时是Content-Disposition
       pos=line.find_first_of("=");
       if (pos==std::string::npos) {
            return ParseResult::BadRequest;
       }
       //区分普通的Content-Disposition和最后带文件名的部分
       auto pos_=line.find_last_of(";");
       if (pos_==std::string::npos) {
        //普通的(只有一个k=v)
           auto tmp=std::move(Trim_(line.substr(pos+1)));
           //要去掉双引号
           file_filed.emplace_back(tmp.substr(1,tmp.size()-3));
       }else {
           //带文件名的
           auto tmp=std::move(Trim_(line.substr(pos+1,pos_-pos)));
           file_filed.emplace_back(tmp.substr(1,tmp.size()-3));
           
           pos=line.find_last_of("=");
           tmp=std::move(Trim_(line.substr(pos+1)));
           file_filed.emplace_back(tmp.substr(1,tmp.size()-2));
       }
    }
     return  ParseResult::Complete;
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
//  //判断是加密成功与否
//  bool HttpRequest::ar_hash_and_versity(){
//      return  authuser.ar_hash_and_versity();
//  }
//  //根据返回值判断是否quary成功
//     bool HttpRequest::SqlQuary(){
//          return  authuser.SqlQuary();
//     }
int  HttpRequest::ConverHex(char ch){
         if(ch>='a'&&ch<='f')   return  ch-'a'+10;
         if(ch>='A'&&ch<='F')   return  ch-'A'+10;
         return  ch-'0';
 }
//   size_t& HttpRequest::get_userid(){
//          return file.get_user_id();
// }
// std::string HttpRequest::get_filename(){
//          return  file.get_filename();
// }

// void HttpRequest::set_Auth_html(){
//      path_="/welcome.html";
// }
// void HttpRequest::set_upload_html(){
//     path_="/upload_success.html";
// }

RouteType HttpRequest::route() const {
    return route_;
}

const std::string& HttpRequest::route_token() const {
    return route_token_;
}
 void HttpRequest::ParseRoute_(){
     // route_ = RouteType::Normal;
    route_token_.clear();

    switch (method_) {

    case HttpMethod::POST:

        if (path_ == "/login.html" ||
            path_ == "/register.html") {
            route_ = RouteType::NeedAuth;
            return;
        }

        if (path_ == "/file") {
            route_ = RouteType::Upload;
            return;
        }

       break;


    case HttpMethod::GET:
      if (path_.starts_with("/file/") && path_.size() > 6) {
        route_ = RouteType::Download;
        return;
      }
      if (path_ == "/welcome.html" || path_ == "/cloud.html" ||
          path_ == "/picture.html" || path_ == "/video.html" ||
          path_ == "/upload_success.html") {
        route_ = RouteType::CloudAccess;
        return;
      }
      break;

     default://普通http
           break;;
    }
     // 验证是post，下载是get
    if (path_ == "/share" ||path_.starts_with("/share/")) {
           ParseShareRoute_();
        }
 }
 void HttpRequest::ParseShareRoute_() {
    constexpr std::string_view SHARE_PATH   = "/share";
    constexpr std::string_view SHARE_PREFIX = "/share/";

    if (path_ == SHARE_PATH) {
        if (method_ == HttpMethod::POST) {
            route_ = RouteType::ShareCreate;
        } 
        //错误则分为normal，在响应报文中返回error.html
        return;
    }

    //   /share/<token>
    //  /share/<token>/verify
    //   /share/<token>/download
    //   /share/<token>/login
    if (!path_.starts_with(SHARE_PREFIX)) {
        return;
    }

    std::string_view rest =
        std::string_view(path_).substr(SHARE_PREFIX.size());

    if (rest.empty()) {
        return;
    }

    const size_t slash = rest.find('/');


    //  GET /share/<token>
     
    if (slash == std::string_view::npos) {
        if (!ValidShareToken_(rest)) {
            return;
        }

        if (method_ != HttpMethod::GET) {
            return;
        }

        route_token_.assign(rest);
        route_ = RouteType::ShareAccess;
        return;
    }

   
    // * /share/<token>/<action>

    if (slash == 0 || slash + 1 >= rest.size()) {
        return;
    }

    // 禁止：
    // /share/token/verify/xxx
    // /share/token/download/xxx
    if (rest.find('/', slash + 1) != std::string_view::npos) {
        return;
    }

    const std::string_view token = rest.substr(0, slash);
    const std::string_view action = rest.substr(slash + 1);

    if (!ValidShareToken_(token)) {
        return;
    }

     // POST /share/<token>/verify

    if (action == "verify") {
        if (method_ != HttpMethod::POST) {
            return;
        }

        route_token_.assign(token);
        route_ = RouteType::ShareVerify;
        return;
    }

   
     // GET /share/<token>/download
     
    if (action == "download") {
        if (method_ != HttpMethod::GET) {
            return;
        }

        route_token_.assign(token);
        route_ = RouteType::ShareDownload;
        return;
    }
     if (action == "login") {
        if (method_ != HttpMethod::POST) {
            return;
        }

        route_token_.assign(token);
        route_ = RouteType::ShareLogin;
        return;
    }
    // 未知 action：
    // /share/token/xxxx
    // 保持 Normal，最后按 404 处理
}
bool HttpRequest::ValidShareToken_(std::string_view token) {
    constexpr size_t kMinTokenLen = 8;
    constexpr size_t kMaxTokenLen = 64;

    if (token.size() < kMinTokenLen ||
        token.size() > kMaxTokenLen) {
        return false;
    }

    for (unsigned char c : token) {
        if (!std::isalnum(c) &&
            c != '-' &&
            c != '_') {
            return false;
        }
    }

    return true;
}
