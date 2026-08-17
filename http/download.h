#pragma once

#include "buffer.h"
#include <cstddef>
#include <list>
#include <string>
enum class DownloadResult {
    Finished,
    NeedWrite,
    Error
};
class Download{
   private:
         std::string filename;
         size_t user_id;
         std::string file_path;
        std::string content_hash;
         size_t  file_size;
         size_t offset;//拖拉
         int   fileFd;
   public:
          ~Download();
          void parase_filed(std::list<std::string>& list);
          void init();

          size_t get_content_length();
          std::string get_filename();

          DownloadResult handle_down(int sockfd);
          size_t& get_userid();
          //打开文件
          bool  openfile();
           bool inited{false};//用于第一次
};