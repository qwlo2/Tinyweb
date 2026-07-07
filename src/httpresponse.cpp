#include "httpresponse.h"
#include "log.h"
#include <fcntl.h>
#include <string>
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap
#include <unordered_map>

const std::unordered_map<std::string, std::string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css"},
    { ".js",    "text/javascript"},
};
const std::unordered_map<int,std::string> HttpResponse::CODE_STATUS{
     {200,"OK"},
    {400,"Bad Request"},
     {403,"Forbidden"},
     {404,"Not Found"},
     {413,"Payload Too Large"}
};
const std::unordered_map<int,std::string> HttpResponse::CODE_PATH{
     {404,"/404.html"},
     {403,"/403.html"},
     {400,"/400.html"},
     {413,"/400.html"}
};
HttpResponse::HttpResponse():code_(-1),isKeepAlive_(false),mmFile_(nullptr){
     path_.clear();
     srcDir_.clear();
     mmFileStat_ = { 0 };
}
HttpResponse::~HttpResponse(){
    UnmapFile();
}
void HttpResponse::Init(const std::string& srcDir, std::string& path, bool isKeepAlive, int code){
       assert(srcDir != "");
       if(mmFile_) { 
        UnmapFile(); 
    }
          isKeepAlive_=isKeepAlive;
          code_=code;
         srcDir_=srcDir;
         path_=path;
         mmFile_=nullptr;
         mmFileStat_ = { 0 };
}
void HttpResponse::UnmapFile(){
    if(mmFile_){
        munmap(mmFile_,mmFileStat_.st_size);
        mmFile_=nullptr;
    }
}
char* HttpResponse::getFile(){
    return mmFile_;
}
size_t HttpResponse::getFileLen() const{
    return mmFileStat_.st_size;
}
void HttpResponse::MakeResponse(Buffer& buff){
    //只会传400，413，200
    if(code_>=400){
        ErrorHtml_();
    }
    else if(stat((srcDir_+path_).data(),&mmFileStat_)||S_ISDIR(mmFileStat_.st_mode)){
        //stat返回0，失败<0（errno来获取错误），S_ISDI是否为目录，st_mode有文件类型和权限
       code_=404;
       ErrorHtml_();
    }
    else if(!(mmFileStat_.st_mode&S_IRUSR)){//判断文件是否可读 IRUSR 文件所有者是否有读权限 S_IROTH 其他用户（非所有者 / 非所属组）是否有读权限
        code_=403;
        ErrorHtml_();
    }
    else {
       code_=200;
    }
    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}
void HttpResponse::ErrorHtml_(){
    if(CODE_PATH.count(code_)){
          path_=CODE_PATH.find(code_)->second;
          stat((srcDir_+path_).data(),&mmFileStat_);//stat获取文件/目录的元数据
    }
}
void HttpResponse::AddStateLine_(Buffer &buff){
    std::string statu_;
    if(CODE_STATUS.count(code_)){
           statu_=CODE_STATUS.find(code_)->second;
    }
    else {
        code_=400;
        statu_=CODE_STATUS.find(code_)->second;
    }
    buff.Append("HTTP/1.1 " +std::to_string(code_)+" "+statu_+"\r\n");
}
void HttpResponse::AddHeader_(Buffer &buff){
    buff.Append("Connection:");
    if(isKeepAlive_){        
        buff.Append("keep-alive\r\n");
        buff.Append("Keep-alive:max=6,timeout=120\r\n");
    }
    else {
       buff.Append("close\r\n");
    }
    buff.Append("Content-type:"+GetFileType_()+"\r\n");
}
void HttpResponse::AddContent_(Buffer &buff){//获取file.size
     int srcfd=open((srcDir_+path_).data(),O_RDONLY);
     if(srcfd<0){
        ErrorContent(buff,"File NotFound");
        return;
     }
     LOG_DEBUG("File Name:%s",(srcDir_+path_).data());
     void* ptr=mmap(0,mmFileStat_.st_size,PROT_READ,MAP_PRIVATE,srcfd,0);
     if(ptr==MAP_FAILED){//MAP_FAILED=(void*)-1
        close(srcfd);
        ErrorContent(buff,"File NotFound");
        return;
     }
     mmFile_=(char*)ptr;
     close(srcfd);
     buff.Append("Content-Length:"+std::to_string(mmFileStat_.st_size)+"\r\n\r\n");
}
void HttpResponse::ErrorContent(Buffer& buff, std::string message){
    std::string body,statu_;
    if(CODE_STATUS.count(code_)){
        statu_=CODE_STATUS.find(code_)->second;
    }
    else {
       statu_="Bad Request";
    }
    body+="<html><title>Error</title>";
    body+="<body style=\"background-color:#ffffff;\">";
    body+=std::to_string(code_)+":"+statu_+"\n";
    body+="<p>"+message+"</p>";
    body+="<hr><em>Tinywebserver</em></body><html>";
    buff.Append("Content-Length: "+std::to_string(body.size())+"\r\n\r\n");
    buff.Append(body);
}
std::string HttpResponse::GetFileType_(){
    size_t indx=path_.find_last_of(".");//成功返回.下标，失败npos
    if(indx==std::string::npos){
        return  "text/plain";//纯文本
    }
    std::string sub=path_.substr(indx);
    if(SUFFIX_TYPE.count(sub)){
        return  SUFFIX_TYPE.find(sub)->second;
    }
    return "text/plain";
}