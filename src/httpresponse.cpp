#include "httpresponse.h"
#include "log.h"

#include <cassert>
#include <string>
#include <unordered_map>
#include <utility>

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
HttpResponse::HttpResponse()
    : code_(-1), isKeepAlive_(false), file_(nullptr) {
    path_.clear();
    srcDir_.clear();
}

HttpResponse::~HttpResponse() = default;

void HttpResponse::Init(const std::string& srcDir, std::string& path,
                        bool isKeepAlive, int code) {
    assert(srcDir != "");
    UnmapFile();
    isKeepAlive_ = isKeepAlive;
    code_ = code;
    srcDir_ = srcDir;
    path_ = path;
}

void HttpResponse::UnmapFile() {
    file_.reset();
}

char* HttpResponse::getFile() {
    return file_
        ? const_cast<char*>(file_->Data())
        : nullptr;
}

size_t HttpResponse::getFileLen() const {
    return file_ ? file_->Size() : 0;
}

void HttpResponse::MakeResponse(Buffer& buff) {
    StaticFileCache& cache = StaticFileCache::Instance();

    if (code_ < 400) {
        StaticFileLookup result = cache.Get(srcDir_ + path_);
        if (result) {
            file_ = std::move(result.file);
            code_ = 200;
        } else {
            code_ = result.status == StaticFileStatus::Forbidden
                ? 403
                : 404;
            ErrorHtml_();
        }
    } else {
        ErrorHtml_();
    }

    if (!file_) {
        StaticFileLookup result = cache.Get(srcDir_ + path_);
        if (result) {
            file_ = std::move(result.file);
        }
    }

    AddStateLine_(buff);
    AddHeader_(buff);
    AddContent_(buff);
}

void HttpResponse::ErrorHtml_() {
    auto it = CODE_PATH.find(code_);
    if (it != CODE_PATH.end()) {
        path_ = it->second;
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
     if(!file_){
        ErrorContent(buff,"File NotFound");
        return;
     }
     LOG_DEBUG("File Name:%s",(srcDir_+path_).data());
     buff.Append("Content-Length:"+std::to_string(file_->Size())+"\r\n\r\n");
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