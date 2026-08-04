#include "upload.h"
#include <string>


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
  user_id = -1;
  user_name = {};
  file_id = -1;
  file_name = {};
  flushed_size = -1;
  ready_flush_size = -1;
  fileds = {};
}