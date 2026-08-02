#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "buffer.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE,//请求行（方法+路径+版本） URL=协议+地址+端口+路径（请求的资源的地址）
        HEADERS,//请求头（附加信息）
        BODY,//请求体
        FINISH,
    };

    enum class ParseResult {
        Complete,
        Incomplete,//没有完全读完
        BadRequest,
        PayloadTooLarge,
        //NeedAuth
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };
    struct Authuser{
        std::string username;
        std::string password;
        std::string ar_hash_pwd;
        bool islogin;
    };
    struct FileEr{
        int user_id;
        std::string user_name;
        int file_id;
        std::string file_name;
        size_t file_size;
        bool isUpload;
    };
    //登录/注册
    Authuser authuser{};
    //文件下载上传
    FileEr  filer{};
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

    bool IsKeepAlive() const;

    void DoAuth(){
         ParsePost_();
    }
 //通过sql验证密码等
    bool SqlQuary();
     //加密与验证
    bool ar_hash_and_versity();
    void is_success(){
       
           path_="/welcome.html";
         
    }

    bool IsAuthRequest() const {
    return method_ == "POST" &&
           (path_ == "/login.html" || path_ == "/register.html");
}
    /*
    todo
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    ParseResult ParseRequestLine_(const std::string& line);//行
    ParseResult ParseHeader_(const std::string& line);//头
    void ParseBody_(const std::string& line);//体

    void ParsePath_();


    void ParsePost_();//只有post才有体
    
    void ParseFromUrlencoded_();//解析体中被加密的密码等

    //通过sql验证密码等
    bool quary_mysql();
    bool quary_lsm();
   
    static bool UserVerify_MYSQL(const std::string& name, const std::string& pwd, bool isLogin);
   static bool UserVerify_LSM( const std::string& name, const std::string& pwd,const char * ip,int port, bool isLogin);

    static std::string Trim_(const std::string& str);
    static std::string ToLower_(std::string str);
    static bool ParseContentLength_(const std::string& value, size_t& len);

    static constexpr size_t MAX_REQUEST_LINE_SIZE = 8 * 1024;
    static constexpr size_t MAX_HEADER_TOTAL_SIZE = 64 * 1024;
    static constexpr size_t MAX_HEADER_COUNT = 100;
    static constexpr size_t MAX_BODY_SIZE = 1024 * 1024;

    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;
   

    size_t contentLength_;
    size_t headerBytes_;
    size_t headerCount_;
    bool hasContentLength_;
    
    static const std::unordered_set<std::string> DEFAULT_HTML;//set只有key，用来判断是否存在
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;//根据html找对应的业务
    static int ConverHex(char ch);
};
