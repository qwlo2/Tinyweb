#pragma  once
#include "buffer.h"
#include "staticfilecache.h"
#include <memory>
#include <string>
#include <unordered_map>
enum class responseResult{
        BadRequest,//400
        PayloadTooLarge,//413
        Complete,//普通的body
        Auth,//登录
        Upload,//上传
       // UploadError,
        Download,//下载
       // DownloadError,
        Unauthorized,//未登录 / Session 失效401
        NotFound,// 404
         ServerError,//500上传/下载服务器内部错误
         ShareCreate,//分享
         ShareAccess,
        ShareVerify,//验证提取码
        ShareDownload
};
class HttpResponse {
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    void MakeResponse(Buffer& buff,responseResult sta);
    void UnmapFile();
    char* getFile();
    size_t getFileLen() const;
    void ErrorContent(Buffer& buff, std::string message);
    int Code() const { return code_; }
     

    void set_filed(std::string name,std::string filed);
    void set_filed(char* name,char* filed);
private:
    void AddStateLine_(Buffer &buff,responseResult& sta);
    void AddHeader_(Buffer &buff,responseResult& sta);
    void AddContent_(Buffer &buff,responseResult& sta);

    void ErrorHtml_();
    std::string GetFileType_();

    int code_;//状态码，响应行：协议+状态码+原因
    bool isKeepAlive_;
 
    std::string path_;//code_对应的html
    std::string srcDir_;
    
    std::shared_ptr<const MappedFile> file_;

    std::unordered_map<std::string,std::string> fileds;
    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;
    static const std::unordered_map<int, std::string> CODE_STATUS;
    static const std::unordered_map<int, std::string> CODE_PATH;
};
