#include "download.h"
#include "log.h"
#include "sqlconnpool.h"
#include <charconv>
#include <cstddef>
#include <ctime>
#include <fcntl.h>
#include <mysql/field_types.h>
#include <sys/sendfile.h>
#include <mysql/mysql.h>
#include <string>
#include <unistd.h>

void Download::parase_filed(std::list<std::string>& list){
    // path=/file/a.txt
    // 已经在para_down_File解决
    filename=list.front();
    if (list.size() > 1) {
       range_header= list.back();
       range_valid=true;
    }
    // if (list.size()>1) {
    //     auto tmp=list.back();
    //     //range:byte=start-
    //     //range:byte=start-end
    //     auto pos=tmp.find_first_of("=");
    //    //b=1-
    //    offset=std::stoi(tmp.substr(pos+1,tmp.size()-pos-2));
    // }

}
 void Download::init(){
         filename={};
          user_id=-1;
          file_path={};
          content_hash={};
           file_size=0;
           offset=0; 
           if (fileFd>0) {
              close(fileFd);
           }
           fileFd=-1;

            range_header={};
            range_start=0;
            range_end=0;
            remaining=0;
            //partial=false;
            range_valid=false;
 }
  Download::~Download(){
      if (fileFd>0) {
              close(fileFd);
           }
  }
size_t& Download::get_userid(){
   return user_id;
}
DownloadResult   Download::openfile(){
      auto sql=SqlConnPool::Instance()->GetConn();
     if (!sql ) {
       return DownloadResult::Error;
     }
       std::string query="SELECT file_size,storage_path,content_hash from object "
                         "where object_id=("
                        " SELECT object_id from file where  user_id="+std::to_string(user_id)+" and file_name=?)";
            auto stmt=mysql_stmt_init(sql.get());
        if (!stmt) {
              return DownloadResult::Error;
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
           return DownloadResult::Error;
         }
       // auto rel=mysql_stmt_bind_result(MYSQL_STMT *stmt, MYSQL_BIND *bnd)
     MYSQL_BIND result[3]{};
     std::uint64_t file_size_db = 0;
     
     result[0].buffer_type = MYSQL_TYPE_LONGLONG;
     result[0].buffer = &file_size_db;
     result[0].is_unsigned = true;

     unsigned long pathsize=84;
     //reserve只代表内存大小，不是元素个数
     file_path.resize(84);
     //data/object/ab/cd/storge_path，也就是64+18=82
     result[1].buffer_type=MYSQL_TYPE_STRING;
     //result[1].buffer=&file_path;
    result[1].buffer= const_cast<char*>(file_path.data());
     result[1].buffer_length=pathsize;
     result[1].length=&pathsize;

    unsigned long hashsize=64;
    content_hash.resize(64);
     result[2].buffer_type=MYSQL_TYPE_STRING;
    // result[2].buffer=&content_hash;
      result[2].buffer=const_cast<char*>(content_hash.data());
     result[2].buffer_length=hashsize;
     result[2].length=&hashsize;

     file_ok=mysql_stmt_bind_result(stmt,result)==0&&
              mysql_stmt_store_result(stmt)==0&&mysql_stmt_fetch(stmt)==0;
    
         mysql_stmt_close(stmt);
         if (!file_ok) {
           return DownloadResult::Error;
         }
         //适配range
        file_size =static_cast<size_t>(file_size_db);
       remaining=file_size;
        range_end = file_size == 0 ? 0 : file_size - 1;

        if (range_valid) {
            auto pos1=range_header.find_first_of("-");
            auto pos2=range_header.find_first_of("=");
            auto tmp=range_header.data();
            if (pos1+1==range_header.size()) {
                // Range: bytes=100-
                
                std::from_chars(tmp+pos2+1,tmp+pos1,offset);
               // offset=std::stoi(range_header.substr(pos2+1,pos1-pos2-1));   
                //sendfile和range的offset都是下标偏移量，数组index
                range_end=remaining-1;
            }else if (pos2+1==pos1) {
                 // Range: bytes=-500,最后500字节
                 size_t off=0;
                 std::from_chars(tmp+pos1+1,tmp+range_header.size(),off);
                 //请求最后n字节，超过即为0
                //  if (file_size<off ) {
                //      off=0;
                //  }else {
                //      offset=static_cast<size_t>(file_size_db)-off;
                //  }
                 offset = off > file_size ? 0 : file_size - off;
                 range_end=remaining-1;
            }else {
               // Range: bytes=100-500
             //  offset=std::stoi(range_header.substr(pos2+1,pos1-pos2-1));
               std::from_chars(tmp+pos2+1,tmp+pos1,offset);
                std::from_chars(tmp+pos1+1,tmp+range_header.size(),range_end);
              // range_end=std::stoi(range_header.substr(pos1+1));
            }
            if (file_size<=offset) {
                      return DownloadResult::RangeError;
                }
              //与上面一样，进行截断
             if (range_end>=file_size) {
                range_end=file_size-1;
             }
             if (offset>range_end) {
                 return DownloadResult::RangeError;
             }
             remaining= range_end-offset+1;
             //0-0就是发生第一个字节
            // remaining=range_end==0&&range_start==0?0:range_end-offset+1;
             range_start=offset;
        }
        fileFd=::open(file_path.c_str(), O_RDONLY);
        if (fileFd<0 ) {
           return DownloadResult::Error;
        }
         return DownloadResult::Finished;
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
    if (file_size==0||remaining==0) {
      return  DownloadResult::Finished;
    }
    constexpr size_t CHUNK_SIZE = 64 * 1024;
   constexpr size_t MAX_SEND_PER_EVENT = 256 * 1024;
   size_t sent_this_time = 0;
   size_t remain=range_end-offset+1;
      off_t offset_ = static_cast<off_t>(offset);
      //限制单次事件处理的发送预算
      while (remain>0) {
    size_t count = std::min(CHUNK_SIZE, remain);

        ssize_t n = ::sendfile(sockfd, fileFd, &offset_, count);
        if (n > 0) {
          //当 offset 参数非空时，sendfile() 本身会更新
          if (offset_==0) {
            //当第一次为0时
             offset_+=n;
          }
           //保存offset状态
           offset=static_cast<size_t>(offset_);

          sent_this_time+=n;
          remain-=n;
          if (sent_this_time>=MAX_SEND_PER_EVENT) {
              return  DownloadResult::NeedWrite;
          }
          continue;
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
       //n > 0是不会有n==0
        if (remain == 0) {
          // 文件已经到 EOF
          return DownloadResult::Finished;
        }
       return DownloadResult::Error;
}   

size_t Download::get_content_length(){
    return  remaining;
}
size_t Download::get_file_size(){
  return  file_size;
}
size_t Download::get_range_start(){
  return  range_start;
}
size_t Download::get_range_end(){
  return  range_end;
}
 bool Download::get_range_valid(){
  return range_valid;
 }
std::string Download::get_filename(){
    return filename;
}
bool Download::share_init(size_t& file_id){
           auto sql=SqlConnPool::Instance()->GetConn();
                  const std::string order = "SELECT user_id ,file_name from file where file_id="+std::to_string(file_id);
                    
              bool sussecc=mysql_query(sql.get(), order.c_str());
              if (sussecc) {
                  LOG_ERROR("share down_init select error");
                  return false;
              }
              MYSQL_RES* res=mysql_store_result(sql.get());
              if (!res) {
               LOG_ERROR(" MYSQL_RES create error");
                  return false;
              }
                MYSQL_ROW row = mysql_fetch_row(res);
               if (!row) {
                 // SELECT 成功，但没有查询到记录
                  LOG_DEBUG("share_download fetch error")
                 mysql_free_result(res);
                 return false;
               }
                unsigned long* len=mysql_fetch_lengths(res);//len[n]每一列的长度
                   
                user_id=std::stoi(std::string(row[0],len[0]));
                filename=std::string(row[1],len[1]);
              mysql_free_result(res);
              return true;
}
