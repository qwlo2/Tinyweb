#include "httprequest.h"
#include <algorithm>
//#include <iostream>
#include <string>
#include <regex>
#include "log.h"
#include "sqlconnpool.h"
const std::unordered_map<std::string,int >   HttpRequest::DEFAULT_HTML_TAG{
         {"/register.html",0} ,{"/login.html",1}
};
 
const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML{
    "register.html","login.html","index.html","welcome.html",
      "vedio.html" ,"pictures.html"
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
          if (lineend==buff.BeginWrite()) {
            if(method_=="post"&&state_==FINISH){//requst只有post、put、patch有body，这里只考虑post
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
std::string HttpRequest::GetPost(const std::string& key) const{
          assert(key!="");
         if(post_.count(key)){
            return post_.find(key)->second;
         }
        return "";
}
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
      if(method_=="POST"&&header_["Content_Type"]=="application/x-www-form-urlencoded"){
          ParseFromUrlencoded_();
          if(DEFAULT_HTML_TAG.count(path_)){
              int tag=DEFAULT_HTML_TAG.find(path_)->second;
              LOG_DEBUG("Tag:%d",tag);
              if(tag==0||tag==1){
              bool islogin=(tag==1);
              if(UserVerify(post_["username"],post_["password"],islogin)){
                     path_="welcome.html";
              }
              else {
                  path_="error.html";
              }
            }
          }
      }
    
}
void HttpRequest::ParseBody_(const std::string& line){
    body_=line;
    ParseFromUrlencoded_();
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
    if(name==""||pwd==""){
        return  false;
      }
      std::shared_ptr<MYSQL> sql=SqlConnPool::Instance()->GetConn();
      assert(sql);

      bool flag=false;
      char order[256]={0};
      MYSQL_RES* res=nullptr;//指向select的结果集
    
    if(isLogin){
      snprintf(order,256,"SELECT username,password FROM user WHERE username='%s' LIMIT 1",name.c_str());
      LOG_DEBUG("%s",order);
      if(mysql_query(sql.get(),order)){//成功返回0，失败非0
          if(res) {
            mysql_free_result(res);
          }
          return flag;
       }
      res=mysql_store_result(sql.get());//将结果传回客户端，mydql_use_result返回指针通过它在服务器中遍历（不free，会阻塞，一次只能一个select）
    //limit 1（0-1）限制了只有一条
      while(MYSQL_ROW row=mysql_fetch_row(res)){
            LOG_DEBUG("MYSQL ROW:%s %s",row[0],row[1]);
            std::string password=row[1];
            if(pwd==password){
                flag=true;
            } 
            else {
               flag=false;
               LOG_DEBUG("PWD ERROR");
            }
        }
    }
    else {
        LOG_DEBUG("register!");
        bzero(order, 256);
       snprintf(order,256,"INSERT INTO user(username,password) VALUES('%s','%s')",name.c_str(),pwd.c_str());
       LOG_DEBUG("%s",order);
       if(mysql_query(sql.get(),order)){
          LOG_DEBUG( "Insert error!");
          return false;
       }
       flag=true;
    }
    if(res) {
        mysql_free_result(res);
    }
    LOG_DEBUG( "UserVerify success!!");
    return  flag;
 }
 int  HttpRequest::ConverHex(char ch){
         if(ch>='a'&&ch<='f')   return  ch-'a';
         if(ch>='A'&&ch<='F')   return  ch-'A';
         return  ch-'0';
 }