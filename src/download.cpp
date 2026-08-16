#include "download.h"
#include "sqlconnpool.h"
#include <fcntl.h>
#include <mysql/field_types.h>
#include <sys/sendfile.h>
#include <mysql/mysql.h>
#include <string>

void Download::parase_filed(std::list<std::string>& list){
    filename=list.front();
    if (list.size()>1) {
        auto tmp=list.back();
        //range:byte=start-
        //range:byte=start-end
        auto pos=tmp.find_first_of("=");
       //b=1-
       offset=std::stoi(tmp.substr(pos+1,tmp.size()-pos-2));
    }
}
 void Download::init(){
         filename={};
          user_id=-1;
          file_path={};
          content_hash={};
           file_size=0;
           offset=0;
           fileFd=-1;
           inited=false;
 }
size_t& Download::get_userid(){
   return user_id;
}
bool   Download::openfile(){
      auto sql=SqlConnPool::Instance()->GetConn();
     if (!sql ) {
       return false;
     }
       std::string query="SELECT file_size,storage_path,content_hash from object "
                         "where object_id=("
                        " SELECT object_id from file where  user_id="+std::to_string(user_id)+" and file_name=?)";
            auto stmt=mysql_stmt_init(sql.get());
        if (!stmt) {
              return false;
        }
        //先init，然后初始化语句，然后绑定参数再执行
        unsigned long file_name_len =
        static_cast<unsigned long>(filename.size());

     MYSQL_BIND file_param{};
    file_param.buffer_type = MYSQL_TYPE_STRING;
    file_param.buffer = const_cast<char*>(filename.data());
    file_param.buffer_length = file_name_len;
    file_param.length = &file_name_len;

         bool file_ok =
        mysql_stmt_prepare(stmt, query.c_str(),
                           static_cast<unsigned long>(query.size())) == 0 &&
        mysql_stmt_bind_param(stmt, &file_param) == 0 &&
        mysql_stmt_execute(stmt) == 0;

        if (!file_ok) {
           return false;
         }
       // auto rel=mysql_stmt_bind_result(MYSQL_STMT *stmt, MYSQL_BIND *bnd)
     MYSQL_BIND result[3]{};
     result[0].buffer_type=MYSQL_TYPE_LONG;
     result[0].buffer=&file_size;
     
     unsigned long pathsize=64;
     result[1].buffer_type=MYSQL_TYPE_STRING;
     result[1].buffer=&file_path;
     result[1].buffer_length=pathsize;
     result[1].length=&pathsize;

    unsigned long hashsize=64;
     result[2].buffer_type=MYSQL_TYPE_STRING;
     result[2].buffer=&content_hash;
     result[2].buffer_length=hashsize;
     result[2].length=&hashsize;

     file_ok=mysql_stmt_bind_result(stmt,result)==0&&
              mysql_stmt_store_result(stmt)==0&&mysql_stmt_fetch(stmt)==0;
    
         mysql_stmt_close(stmt);
         if (!file_ok) {
           return false;
         }
         
        fileFd=::open(file_path.c_str(), O_RDONLY);
        if (fileFd<0 ) {
           return false;
        }
         return true;
//  mysql_stmt_prepare()
//         ↓
// mysql_stmt_bind_param()   // 绑定输入参数
//         ↓
// mysql_stmt_execute()
//         ↓
// mysql_stmt_bind_result()  // 绑定结果缓冲区
//         ↓
// mysql_stmt_store_result() // 可选：先把结果集缓存到客户端
//         ↓
// mysql_stmt_fetch()        // 一行一行取
}
  DownloadResult Download::handle_down(int sockfd){
    constexpr size_t CHUNK_SIZE = 64 * 1024;
   constexpr size_t MAX_SEND_PER_EVENT = 256 * 1024;
   size_t sent_this_time = 0;

      off_t offset_ = static_cast<off_t>(offset);
       size_t remain=0;
      //限制单次事件处理的发送预算
      while (offset_< file_size &&
       sent_this_time < MAX_SEND_PER_EVENT) {

         remain =static_cast<size_t>(file_size - offset_);

    size_t count = std::min(CHUNK_SIZE, remain);

        ssize_t n = ::sendfile(sockfd, fileFd, &offset_, count);
        //保存offset状态
        offset=offset_;
        if (n > 0) {
          //当 offset 参数非空时，sendfile() 本身会更新
         // offset_ += n;
          sent_this_time+=n;
          continue;
        }

        if (n == 0) {
          // 文件已经到 EOF
          return DownloadResult::Finished;
        }

        if (errno == EINTR) {
          // 被信号中断，重新调用
          continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          // socket 发送缓冲区满
          // 保存当前 offset，监听 EPOLLOUT
          return DownloadResult::NeedWrite;
        }

        // 其他错误
        // 下载失败
        break;
      }
       return DownloadResult::Error;
}   

size_t Download::get_content_length(){
    return  file_size-offset;
}
std::string Download::get_filename(){
    return filename;
}