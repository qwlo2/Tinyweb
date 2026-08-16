#include "upload.h"
#include "buffer.h"
#include "sqlconnpool.h"
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <openssl/evp.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>


void UploadFile::parase_filed(std::list<std::string>& list){
     int i=1;
     std::string key;
      for (auto& it : list) {
           if (i%2) {
               key=it;
           }else {
               fileds.emplace(key,it);
           }
           ++i;
      }
}
void UploadFile::init(){
  user_id = 0;
  file_id = 0;
  writed_size = 0;
  ready_write_size = 0;
  fileds = {};
  file_fd=-1;
  inited=false;
  hash_ctx_=nullptr;
  temp_path.clear();
  boundary={};
}
size_t& UploadFile::get_user_id(){
     return  user_id;
}
std::string& UploadFile::get_filename(){
      return  fileds["file"];
}
void UploadFile::incr_writed_size(size_t num){
     writed_size+=num;
}
void UploadFile::incr_ready_write_size(size_t num){
     ready_write_size+=num;
}
std::string& UploadFile::get_boundary(){
     return boundary;
 }
 Upload UploadFile::upload_file(int file_fd,Buffer& readBuff_){
       //因为结束符不一定是连贯的
       //\r\n--boundary-- \r\n
       //因为可能有粘包，因此只能找到/r/n，并保留它之后的所有
         int safe_size=0;
          bool is_end=false;
      auto pos=std::move(readBuff_.find_of_first("="));
      if (pos==std::string::npos) {
          //没有就全是文件数据
            safe_size=readBuff_.ReadableBytes();
      }else {
        //进行匹配，必须要同时存在，不然无法把/r/n去除
            is_end=std::string(pos,4+boundary.size()+4)=="--"+boundary+"--";
            //只读到/r/n
            safe_size=pos;
      }
      
       ready_write_size+=safe_size;
       int saveErrno=0;
       //hash的data和len
       auto datas=readBuff_.Peek();
       size_t len=safe_size;

       bool ret=readBuff_.WriteFd(file_fd, safe_size);
       writed_size+=safe_size;
       //进行增量hash
       if (!chunkhash(datas,len)) {
             return Upload::UploadError;
       }
       readBuff_.Retrieve(safe_size);
       if (is_end&&ret) {
        readBuff_.Retrieve(8+boundary.size()+1);
          //响应报文
           return Upload::ReadyWrite;
       }else if (!is_end&&ret ) {
          //文件没有上传完成
          return  Upload::NeedRead;
       }else {
          //返回重传
          return Upload::UploadError;
       }
 }
 //页缓存是内核维护的、可回写和可回收的中间缓冲。当磁盘跟不上时，
 // 内核会让 write() 变慢，从而把压力逐层传回网络端。因此即便 write() 后数据暂时还在内存中，整个上传依然是流式的。
Upload UploadFile::handle_upload_file(Buffer& readBuff_){
    
         auto  ret=std::move(upload_file(file_fd,readBuff_));
       //将剩余的移动到前方，防止缓冲区无线扩大
       readBuff_.adjust_pos();
       //上传完毕
       if (ret==Upload::ReadyWrite) {
          //rename,sync
          std::string hash_hex;
           if ( !finishHash(hash_hex)) {
               EVP_MD_CTX_free(hash_ctx_);
              close(file_id);
              return Upload::UploadError;
           }
           //要先创建目录
          std::filesystem::path fina_dire="data/object"+hash_hex.substr(0,2)+"/"+hash_hex.substr(2,2);
          //exists判断文件/目录是否存在，可能是文件存在
          if (!std::filesystem::is_directory(fina_dire)) {
              std::filesystem::create_directory(fina_dire);
          }
          std::filesystem::path fina_path=fina_dire/hash_hex;
            fsync(file_fd);
          if (!rename_file(fina_path)) {
              EVP_MD_CTX_free(hash_ctx_);
              close(file_id);
              return Upload::UploadError;
          }
       }else if (ret==Upload::UploadError ) {
        //失败
           EVP_MD_CTX_free(hash_ctx_);
             close(file_id);
       }
      return  ret;
 }
 bool UploadFile::init_fileds(){
     //增量hash初始化
     //摘要上下文创建
       hash_ctx_=EVP_MD_CTX_new();//哈希摘要（md5，sha256），还有对称加密
       if (!EVP_MD_CTX_init(hash_ctx_)) {
           EVP_MD_CTX_free(hash_ctx_);
           return false;
       }
       //指定算法，第三个参数指定硬件engine，nulpte默认
       if (EVP_DigestInit_ex(hash_ctx_,EVP_sha256(), nullptr)!=1) {
           EVP_MD_CTX_free(hash_ctx_);
             hash_ctx_ = nullptr;
            return false;
       }
     //临时文件
      if (!std::filesystem::is_directory("data/tmp")) {
             std::filesystem::create_directory("data/tmp");
        }
       if (!std::filesystem::is_directory("data/object")) {
             std::filesystem::create_directory("data/object");
        }
        //返回值可以有成功，失败，文件重传
        temp_path="data/tmp/"+std::to_string(get_user_id())+"_"+get_filename();
      //  std::string tmp_file("data/tmp"+std::to_string(get_user_id())+"_"+get_filename());
        file_fd=::open(
         temp_path.c_str(), 
         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
          0600);
       //  O_WRONLY   只写
       // O_CREAT    文件不存在就创建
       // O_EXCL     文件已存在则失败，避免覆盖
        // O_CLOEXEC  exec 时自动关闭
          // 0600       只有服务器进程所属用户可读写
          if (file_fd<0) {
             EVP_MD_CTX_free(hash_ctx_);
             close(file_id);
             return false;
          }
        return true;
 }
 //增连hash
 bool  UploadFile::chunkhash(const char* data,size_t len){
        if (EVP_DigestUpdate(hash_ctx_, data, len)!=1) {
          EVP_MD_CTX_free(hash_ctx_);
             close(file_id);
             return false;
        }
        return true;
 }
 bool  UploadFile::finishHash(std::string& hash_hex){
     if (!hash_ctx_) {
         return false;
     }
      unsigned char digest[EVP_MAX_MD_SIZE];
      unsigned int digest_len=0;
      if (EVP_DigestFinal_ex(
            hash_ctx_,
            digest,
            &digest_len
        ) != 1) {
        return false;
    }

    hash_hex = ToHex(digest, digest_len);
    return true;
 }
 std::string UploadFile::ToHex( const unsigned char* digest,unsigned int length) {
     std::ostringstream ost;
     //设定规则将输入到ost的都转化为16进制并补0
     ost<<std::hex<<std::setfill('0');
     for (int i=0;i<length;++i) {
          //输出流对字符类型通常会按照“字符”处理，而不是按照数字处理
         // 原因是 std::hex 只影响整数数值格式化重载,字符不会进行转换
        ost<<std::setw(2)<<static_cast<unsigned int>(digest[i]);
     }
     return  ost.str();
 }
 bool UploadFile::rename_file(std::filesystem::path& fina_path){
// rename 时 fd 不必关闭，但上传状态会更清晰。
    if (::close(file_fd) == -1) {
        return false;
    }
    // 2. 目标不存在时才原子发布。
    const long ret = ::syscall(
        SYS_renameat2,
        AT_FDCWD,
        temp_path.c_str(),
        AT_FDCWD,
        fina_path.c_str(),
        RENAME_NOREPLACE//目标路径已经存在时，禁止覆盖。
    );

    //普通的rename会出现竞争，会将存在的文件覆盖，要保证原子性，renameat2是若存在则返回
    int dir_fd = ::open(
    fina_path.parent_path().c_str(),
    O_RDONLY | O_DIRECTORY | O_CLOEXEC
     );
    
     //路径修改持久化
    if (dir_fd == -1) {
         return false;
   }
   //将目录修改落盘
    if (::fsync(dir_fd) == -1) {
    ::close(dir_fd);
    return false;
   }
   //fsync(fd)
// → 同步一个文件

// syncfs(fd)
// → 同步 fd 所在的整个文件系统

// sync()
// → 同步系统里所有挂载文件系统的脏数据
   ::close(dir_fd);
    if (ret == 0&& add_or_increment_object(fina_path)) {
        return true;
    }

    if (errno == EEXIST&& add_or_increment_object(fina_path)) {
        // 已有相同哈希文件，删除当前临时文件。
        ::unlink(temp_path.c_str());
        return true;;
    }

    return false;
 }

 // 在同一事务中增加物理对象引用，并建立用户逻辑文件记录。
 bool UploadFile::add_or_increment_object(std::filesystem::path& final_path){
    auto mysql_ = SqlConnPool::Instance()->GetConn();
    if (!mysql_ || user_id == static_cast<size_t>(-1) ||
        get_filename().empty()) {
        return false;
    }

    MYSQL* connection = mysql_.get();
    const std::string content_hash = final_path.filename().string();
    const std::string storage_path = final_path.string();
    const std::string file_name = get_filename();

    if (mysql_query(connection, "START TRANSACTION") != 0) {
        return false;
    }

    const std::string object_sql =
        "INSERT INTO object "
        "(content_hash, file_size, storage_path, ref_count) "
        "VALUES ('" + content_hash + "', " +
        std::to_string(writed_size) + ", '" + storage_path + "', 1) "
        "ON DUPLICATE KEY UPDATE "
        "object_id = LAST_INSERT_ID(object_id), "
        "ref_count = ref_count + 1";
   //object_id = LAST_INSERT_ID(object_id)会将LAST_INSERT_ID设置object_id
   //随后返回object_id，这样mysql_insert_id就可以获得当前的会将LAST_INSERT_ID设置object_id
    if (mysql_query(connection, object_sql.c_str()) != 0) {
        mysql_rollback(connection);
        return false;
    }

    // 新对象返回自增 ID；重复对象通过 LAST_INSERT_ID(object_id)
    // 返回已有对象的 ID。
    std::uint64_t object_id =
        static_cast<std::uint64_t>(mysql_insert_id(connection));
    if (object_id == 0) {
        mysql_rollback(connection);
        return false;
    }

    const std::string file_sql =
        "INSERT INTO `file` (user_id, file_name, object_id) "
        "VALUES (" + std::to_string(user_id) + ", ?, " +
        std::to_string(object_id) + ")";

    MYSQL_STMT* file_stmt = mysql_stmt_init(connection);
    if (!file_stmt) {
        mysql_rollback(connection);
        return false;
    }

    unsigned long file_name_len =
        static_cast<unsigned long>(file_name.size());

    MYSQL_BIND file_param{};
    file_param.buffer_type = MYSQL_TYPE_STRING;
    file_param.buffer = const_cast<char*>(file_name.data());
    file_param.buffer_length = file_name_len;
    file_param.length = &file_name_len;

    const bool file_ok =
        mysql_stmt_prepare(file_stmt, file_sql.c_str(),
                           static_cast<unsigned long>(file_sql.size())) == 0 &&
        mysql_stmt_bind_param(file_stmt, &file_param) == 0 &&
        mysql_stmt_execute(file_stmt) == 0;
    mysql_stmt_close(file_stmt);

    if (!file_ok || mysql_commit(connection) != 0) {
        mysql_rollback(connection);
        return false;
    }

    return true;
 }
