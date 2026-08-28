#include "httpresponse.h"
#include "log.h"

#include <cassert>
#include <string>
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
     {413,"Payload Too Large"},
     {500 ,"Internal Server Error"},
     {401 ,"Unauthorized"},
     {206, "Partial Content"},
   {416, "Range Not Satisfiable"}
};
const std::unordered_map<int,std::string> HttpResponse::CODE_PATH{
     {404,"/404.html"},
     {401,"/401.html"},
     {403,"/403.html"},
     {400,"/400.html"},
     {413,"/413.html"},
     {500,"/500.html"},
         {416,"/416.html"}
};
HttpResponse::HttpResponse()
    : code_(-1), isKeepAlive_(false), file_(nullptr) {
    path_.clear();
    srcDir_.clear();
}

HttpResponse::~HttpResponse() = default;

void HttpResponse::Init(const std::string& srcDir,std::string& path,
                        bool isKeepAlive, int code) {
    assert(srcDir != "");
    UnmapFile();
    isKeepAlive_ = isKeepAlive;
    code_ = code;
    srcDir_ = srcDir;
   if (code>=400) {
       path_ = CODE_PATH.find(code_)->second;
   }else {
      path_=path;
   }
    // has_cookies=false;
    // is_download=false;
   // fileds={};
   //在init前可能就设置某些字段，因此在MakeResponse后重置
     file_={};
}
void HttpResponse::set_filed(std::string name,std::string filed){
     fileds.emplace(name,filed);
}
void HttpResponse::set_filed(char* name,char* filed){
    fileds.emplace(name,filed);
}
 std::string HttpResponse::get_filed(const std::string& name){
    if (fileds.contains(name)) {
     return fileds[name];
    }
    return "";
 }
std::string HttpResponse::get_filed(const char*& name){
    if (fileds.contains(name)) {
     return fileds[name];
    }
    return "";
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

void HttpResponse::MakeResponse(Buffer& buff,responseResult sta) {
    if (sta == responseResult::Download) {
        file_.reset();

        AddStateLine_(buff, sta);
        AddHeader_(buff, sta);
        AddContent_(buff, sta);

        fileds.clear();
        return;
    }

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

    AddStateLine_(buff,sta);
    AddHeader_(buff,sta);
    AddContent_(buff,sta);
    fileds={};
}

void HttpResponse::ErrorHtml_() {
    auto it = CODE_PATH.find(code_);
    if (it != CODE_PATH.end()) {
        path_ = it->second;
    }
}

void HttpResponse::AddStateLine_(Buffer &buff,responseResult& sta){
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
void HttpResponse::AddHeader_(Buffer &buff,responseResult& sta){
    buff.Append("Connection:");
    if(isKeepAlive_){        
        buff.Append("keep-alive\r\n");
        buff.Append("Keep-alive:max=6,timeout=120\r\n");
    }
    else {
       buff.Append("close\r\n");
    }
    if (sta==responseResult::Auth||sta==responseResult::ShareLogin) {
    //path决定浏览器访问哪些 URL 路径时，应该携带这个 Cookie。
    //HttpOnly代表浏览器可以保存它、发送它，但是网页里的 JavaScript 不能直接读取它。
    //Secure表示这个 Cookie 只通过 HTTPS 请求发送。
    //SameSite 主要限制：从其他网站发起的请求，浏览器要不要携带你的 Cookie。
       buff.Append("Set-Cookie:session="+fileds["cookie"]+"; Path=/; HttpOnly;  SameSite=Lax\r\n");
    }
    if (sta == responseResult::Download) {
      buff.Append("Content-Type: application/octet-stream\r\n");
    } else {
      buff.Append("Content-Type: " + GetFileType_() + "\r\n");
    }
    if (sta==responseResult::ShareCreate) {
         //https，由于我没有ssl/tls，因此暂时不管，Path=/file指挥在上传下载时发送cookie，因此设计为所有都会发，只有需要的才处理
        // buff.Append("Set-Cookie:share_token="+fileds["share_token"]+"; Path=/; HttpOnly; SameSite=Lax\r\n");
         buff.Append("Share-Token: " + fileds["share_token"] + "\r\n");

         if (!fileds["code"].empty()) {
           buff.Append("Code: " + fileds["code"] + "\r\n");
         }
        }
}
void HttpResponse::AddContent_(Buffer &buff,responseResult& sta){//获取file.size
    //这里还写入Content，因此要返回
    //file取决于是否有对应的html文件，因此这里的顺序暂定
    //下载时是先把响应报文发完，在发文件，html的位置暂定
    if (sta==responseResult::Download) {
        buff.Append( "Accept-Ranges:"+fileds[ "Accept-Ranges"]+"\r\n");
        if (fileds["range_valid"]=="true") {
               buff.Append( "Content-Range:"+fileds[ "Content-Range"]+"\r\n");
        }
        buff.Append( "Content-Length: "+fileds[ "Content-Length: "]+"\r\n");
        buff.Append("Content-Disposition: attachment; filename=\"" +
                    fileds["filename"] + "\"\r\n\r\n");
        return;
    }
    if (sta==responseResult::RangeError) {
          buff.Append( "Content-Range:"+fileds[ "Content-Range"]+"\r\n");
    }
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
