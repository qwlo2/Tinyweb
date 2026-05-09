#pragma once
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
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string getpath() const;
    std::string& getpath();
    std::string getmethod() const;
    std::string getversion() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /* 
    todo 
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void ParseBody_(const std::string& line);

    void ParsePath_();
    void ParsePost_();
    void ParseFromUrlencoded_();

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);

    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;

    static const std::unordered_set<std::string> DEFAULT_HTML;//set只有key，用来判断是否存在
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;//根据html找对应的业务
    static int ConverHex(char ch);
};