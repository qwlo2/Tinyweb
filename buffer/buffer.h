#pragma once
#include <cstddef>
#include <string>
#include <cstring>
#include <vector>
#include <atomic>
#include <cassert>
#include <sys/uio.h>
#include <unistd.h>
class Buffer{
    public:
      Buffer(int initBuffersize=1024);
      ~Buffer()=default;

     size_t WritableBytes() const;       
    size_t ReadableBytes() const ;
    size_t PrependableBytes() const;

    const char* Peek() const;
    void EnsureWriteable(size_t len);
    void HasWritten(size_t len);

    void Retrieve(size_t len);
    void RetrieveUntil(const char* end);

    void RetrieveAll() ;
    std::string RetrieveAllToStr();

    const char* BeginWriteConst() const;
    char* BeginWrite();

    void Append(const std::string& str);
    void Append(const char* str, size_t len);
    void Append(const void* data, size_t len);
    void Append(const Buffer& buff);
    //以peek为起始的pos
    std::string::size_type find_of_first(std::string& res);
     std::string::size_type find_of_first(const char* res);

    ssize_t ReadFd(int fd, int* saveErrno);
    bool WriteFd(int fd, int&  len);
    //解析报文时由于每次都不一定能够解析完整，因此会残留
    //导致rpos前方的空间被浪费
    //上传文件等每次都要保留------boundary--以防止结束符被写入文件
    //最终导致缓冲区被迫达到GB级
    void adjust_pos();
    private:
    char* BeginPtr_();
    const char* BeginPtr_() const;
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::size_t readPos_;//标记可读位置
    std::size_t writePos_;//标记可写位置
};