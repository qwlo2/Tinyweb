#pragma once
#include <cstddef>
#include <list>
#include <string>
enum class DownloadResult {
    Finished,
    NeedWrite,
    Error,
    RangeError
};
class Download{
   private:
         std::string filename;
         size_t user_id;
         std::string file_path;
        std::string content_hash;
         size_t  file_size;

         std::string range_header{""};//实现range
          size_t offset;//拖拉
          size_t range_start{0};//保持offset
         size_t range_end{0};
         size_t remaining{0};//剩余的，尚未处理的
         //bool partial{false};//不完全的
         bool range_valid{false};
         int   fileFd{-1};
   public:
          ~Download();
          void parase_filed(std::list<std::string>& list);
          bool share_init(size_t& file_id);
          void init();

          size_t get_content_length();
          size_t get_file_size();
          size_t get_range_start();
          size_t get_range_end();
          std::string get_filename();
          bool get_range_valid();

          DownloadResult handle_down(int sockfd);
          size_t& get_userid();
          //打开文件
          DownloadResult  openfile();
           bool inited{false};//用于第一次

          
};