#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "Auth.h"
#include "buffer.h"
#include "download.h"
#include "upload.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE,//请求行（方法+路径+版本） URL=协议+地址+端口+路径（请求的资源的地址）
        HEADERS,//请求头（附加信息）
        BODY,//请求体
        FILR_BODY,//file请求体
        FINISH,
    };

    enum class ParseResult {

        Incomplete,//没有完全读完
        BadRequest,
        PayloadTooLarge,
        Complete,//普通的body
        NeedAuth,//登录
        Upload,//上传
        UploadError,
        Download,//下载
        DownloadError
    };
    // size_t& get_userid();s
    // std::string get_filename();

    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    ParseResult parse(Buffer& buff);

    std::string getpath() const;
    std::string& getpath();
    std::string getmethod() const;
    std::string getversion() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;
     std::string Getheader(const std::string& key) const;
    std::string Getheader(const char* key) const;

    void paraAuth(Auth& auther);
    void para_up_File(UploadFile& filer);
    void para_down_File(Download& filer);

    bool IsKeepAlive() const;

//  //通过sql验证密码等
//     bool SqlQuary();
//      //加密与验证
//     bool ar_hash_and_versity();
    //注册/登录成功，改变path
    void is_success(){
           path_="/welcome.html";
    }
    ParseResult rece_uploadfile();
  ParseResult  parseResult() ;

private:
    ParseResult ParseRequestLine_(const std::string& line);//行
    ParseResult ParseHeader_(const std::string& line);//头
    void ParseBody_(const std::string& line);//体
    ParseResult ParseFileBody(const std::string& line);//文件

    void ParsePath_();

    void ParsePost_();//只有post才有体
    
    void ParseFromUrlencoded_();//解析体中被加密的密码等

    static bool UserVerify_MYSQL(const std::string& name, const std::string& pwd, bool isLogin);
   static bool UserVerify_LSM( const std::string& name, const std::string& pwd,const char * ip,int port, bool isLogin);
    
   // =时kv的左右2边可以有空格，但是k里面不能有，v可以有
   //：时k与：要紧挨，其他与=相同
    static std::string Trim_(const std::string& str);
    static std::string ToLower_(std::string str);
    static bool ParseContentLength_(const std::string& value, size_t& len);

    static constexpr size_t MAX_REQUEST_LINE_SIZE = 8 * 1024;
    static constexpr size_t MAX_HEADER_TOTAL_SIZE = 64 * 1024;
    static constexpr size_t MAX_HEADER_COUNT = 100;
    static constexpr uint64_t MAX_BODY_SIZE = 4ULL*1024 * 1024*1024;

    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;
     std::list<std::string> file_filed;//由于不是kv形式的报文，因此把所有解析的都放在这里，最后组装

    size_t contentLength_;
    size_t headerBytes_;
    size_t headerCount_;
    bool hasContentLength_;
    bool ready_rece_data{false};

    static const std::unordered_set<std::string> DEFAULT_HTML;//set只有key，用来判断是否存在
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;//根据html找对应的业务
    static int ConverHex(char ch);
};
