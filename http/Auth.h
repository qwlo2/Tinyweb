#pragma once

#include <cstdint>
#include <string>
constexpr uint32_t ARGON2_TIME_COST = 2;
constexpr uint32_t ARGON2_MEMORY_COST = 1 << 15; // KiB, 32 MiB
constexpr uint32_t ARGON2_PARALLELISM = 1;
constexpr size_t ARGON2_SALT_LEN = 16;
constexpr size_t ARGON2_HASH_LEN = 32;
constexpr size_t ARGON2_ENCODED_LEN = 256;
class Auth{
   private:
        std::string username;
        std::string password;
        std::string ar_hash_pwd;
        bool islogin;

        //salt
        bool FillRandomBytes(unsigned char* data, size_t len);
        //加密
        bool HashPasswordArgon2id(const std::string& password, std::string& encoded);
        //验证
        bool VerifyPasswordArgon2id(const std::string& encoded, const std::string& password);

         bool quary_mysql();
         bool quary_lsm();
   
   public:
    //验证或注册
    bool ar_hash_and_versity();
    bool SqlQuary();
    //将quary和加密结合
    bool Auth_ar_and_SqlQuary();
    
    void init();
    void setUsername(std::string& name);
    void setPasaword(std::string& word);
    void setAr_hash_pwd(std::string& pwd);
    void setIslogin(bool& islogin_);
     std::string& getUsername();
    std::string& getPasaword();
   std::string& getAr_hash_pwd();
    bool& getIslogin();
};