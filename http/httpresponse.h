#pragma  once
#include "buffer.h"
#include "staticfilecache.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff);
    void UnmapFile();
    char* getFile();
    size_t getFileLen() const;
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }
    void set_has_cookies(bool has_cookies_){
         has_cookies=has_cookies_;
    }
    bool& get_has_cookies(){
          return  has_cookies;
    }
     
    void set_isdownload(bool res);
    bool& get_isdownload(){
         return is_download;
    }

    void set_filed(std::string name,std::string filed);
    void set_filed(char* name,char* filed);
private:
    void AddStateLine_(Buffer &buff);
    void AddHeader_(Buffer &buff);
    void AddContent_(Buffer &buff);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;//状态码，响应行：协议+状态码+原因
    bool isKeepAlive_;
 
    std::string path_;//code_对应的html
    std::string srcDir_;
    
    std::shared_ptr<const MappedFile> file_;
    bool has_cookies{false};
    bool is_download{false};

    std::unordered_map<std::string,std::string> fileds;
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};