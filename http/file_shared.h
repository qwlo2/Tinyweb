#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
struct ShareListItem {
     std::string file_name;
     std::string share_token;
     bool has_code;
     std::optional<std::string> expire_time;
     std::string created_at;
};
class File_shared{
   private:
    std::optional<std::string> get_share_token();
    char* get_code(int bits);
  
     bool valid_filename(const std::string& filename);
     bool valid_share_token(const std::string& token);
   public:
     static  File_shared*  Instance();
     //此时也要验证登录，来获取user-id
     std::optional<std::pair<std::string,std::string>>  share_file(const std::string& has_code,size_t& user_id,const std::string& filename,const std::string& time);
     //验证码通过进入是否下载html
     bool versity_share_token(const std::string& token,const std::string& code);
     //验证下载时是否有效,随后进入download
     bool versity_doenload(size_t& file_id,std::string& auth_hash);
     //验证是否需要code
     //bool没有区分开code是否为null，或者错误
     //返回是否需要code
     std::string vsersity_ShareAccess(const std::string& token);
     std::optional<std::vector<ShareListItem>> list_shares(std::size_t user_id);
     bool cancel_share(std::size_t user_id,
                       const std::string& filename,
                       const std::string& created_at);
};
// 1. GET  /share/<token>
//    → 展示文件信息/提取码页面

// 2. POST /share/<token>/verify
//    → 验证4位提取码
//    → 成功后发短期 HttpOnly share_auth Cookie

// 3. GET  /share/<token>/download
//    → 检查分享状态 + share_auth
     //这里是检查下载时，分享是否取消，未取消根据auth——hash判断share-id是否相等（采用reids，有时效性）
//    → file → object
//    → 复用现有 Range + sendfile 下载
