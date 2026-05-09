#pragma once
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

    ssize_t ReadFd(int fd, int* saveErrno);
    ssize_t WriteFd(int fd, int* saveErrno);

    private:
    char* BeginPtr_();
    const char* BeginPtr_() const;
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::atomic<std::size_t> readPos_;//标记可读位置
    std::atomic<std::size_t> writePos_;//标记可写位置
};