#pragma  once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
class UploadFile{
    private:
        int user_id;
        std::string user_name;
        int file_id;
        std::string file_name;
        size_t flushed_size;
        size_t ready_flush_size;

        std::unordered_map<std::string, std::string> fileds;
    public:
       void init();
       void parase_filed(std::list<std::string>& list);
};