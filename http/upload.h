#pragma  once

#include "buffer.h"
#include <cstddef>
#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>
#include <openssl/evp.h>
enum class Upload{
      NeedRead,
      ReadyWrite,
      UploadError
};
class UploadFile{
    private:
        size_t user_id;
        int file_id;
        size_t writed_size;
        std::string boundary;
        size_t ready_write_size;
        EVP_MD_CTX* hash_ctx_ { nullptr};
        std::filesystem::path temp_path;
        int file_fd{-1};
        std::unordered_map<std::string, std::string> fileds;
    public:
       void init();
       ~UploadFile();
       //字段初始化
       void parase_filed(std::list<std::string>& list);
       bool inited{false};

       size_t& get_user_id();
       std::string& get_filename();
       std::string&  get_boundary();
       
       void incr_writed_size(size_t num);
       void incr_ready_write_size(size_t num);
       //处理文件存储
       Upload handle_upload_file(Buffer& readBuff_);
       //只要write到内核成功就可以，否则为失败，停止传输，返回文件重传
       Upload upload_file(int file_fd,Buffer& readBuff_);
       //OpenSSL （https）增量 SHA-256，evp，增强验证包，md5/sha256是加密算法，
       bool init_fileds();
       bool chunkhash(const char* data,size_t len);
       //获取256bit并转化为16进制字符
       bool  finishHash(std::string& hash_hex);
       //hash_hex从256bit转化为32字节16进制的字符
       std::string ToHex( const unsigned char* digest,unsigned int length); 

       //将文件进行落盘，并决定是新增object还是增加引用数量
       bool rename_file(std::filesystem::path& fina_path);
       //增加object或者引用数量
        bool add_or_increment_object(std::filesystem::path& fina_path);
};