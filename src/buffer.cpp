#include "buffer.h"
#include <cstddef>
#include <sys/types.h>
#include <sys/uio.h>
Buffer::Buffer(int initBuffersize):buffer_(initBuffersize),readPos_(0),writePos_(0){

}
// Buffer::~Buffer(){

// }
//这3个可读写,预添加的Bytes
size_t Buffer::WritableBytes() const{
    return buffer_.size()-writePos_;
}      
size_t Buffer::ReadableBytes() const{
    return writePos_-readPos_;
}
size_t Buffer::PrependableBytes() const{
     return  readPos_;//添加在头部的数据
}

const char* Buffer::Peek() const{
      return  BeginPtr_()+readPos_;
}

void Buffer::EnsureWriteable(size_t len){
      if(WritableBytes()<len){
        MakeSpace_(len);
      }
      assert(WritableBytes()>=len);
}

//has write
void Buffer::HasWritten(size_t len){
     writePos_+=len;
}

//读取len个，或者读到end
void Buffer::Retrieve(size_t len){
    assert(len<=ReadableBytes());
    readPos_+=len;
}
void Buffer::RetrieveUntil(const char* end){
    assert(Peek()<=end);
    Retrieve(end-Peek());
} 
//读取all，或读到str
void Buffer::RetrieveAll(){
    bzero(buffer_.data(),buffer_.size());
    readPos_=0;
    writePos_=0;
}
std::string Buffer::RetrieveAllToStr(){
    std::string str(Peek(),ReadableBytes());
    RetrieveAll();
      return str;
}
//获取写开始的指针
const char* Buffer::BeginWriteConst() const{
    return BeginPtr_()+writePos_;
}
char* Buffer::BeginWrite(){
     return BeginPtr_()+writePos_;
}
//统一接口，以char*的为核心接口
void Buffer::Append(const char* str, size_t len){
    assert(str);
    EnsureWriteable(len);
    std::copy(str,str+len,BeginWrite());
     HasWritten(len);
}
void Buffer::Append(const std::string& str){
    Append(str.data(), str.size());
}
void Buffer::Append(const void* data, size_t len){
     assert(data);
    Append(static_cast<const char*>(data),len);
}
void Buffer::Append(const Buffer& buff){
    Append(buff.Peek(),buff.ReadableBytes());
}
//fd中读取到buffer
ssize_t Buffer::ReadFd(int fd, int* saveErrno){
    char buff[65535];//单次系统调用高效读取的上限，linux不建议超过64kb
     iovec iov[2];//2个地方分别是buffer和临时缓冲区，配合readv，writev，read/write依次写满
     const size_t save_writableBytes=WritableBytes();
    iov[0].iov_base=BeginWrite();
    iov[0].iov_len=save_writableBytes;
    iov[1].iov_base=buff;
    iov[1].iov_len=sizeof(buff);

    const ssize_t len=readv(fd, iov,2);
    if(len<0){
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len)<=save_writableBytes){
         HasWritten(len);
    }
    else  {
        writePos_=buffer_.size();
        Append(buff,len-save_writableBytes);
    }
    return len;
}
//向fd中写入
ssize_t Buffer::WriteFd(int fd, int* saveErrno){
    size_t save_ReadableBytes=ReadableBytes();
    ssize_t len=write(fd,Peek(),save_ReadableBytes);
    if(len<0){
        *saveErrno=errno;
    }
    else {
         readPos_+=len;
    }
     return len;
}
char* Buffer::BeginPtr_(){
    //return  &*buffer_.begin();
    return buffer_.data();
}
const char* Buffer::BeginPtr_() const{
   return  buffer_.data();
}
//调整空间
void Buffer::MakeSpace_(size_t len){
     if(WritableBytes()+PrependableBytes()<len){
          buffer_.resize(writePos_+len+1);//防止/0
     }
     else {
        char* ch=BeginPtr_();
        size_t save=ReadableBytes();
         std::copy(ch+readPos_,ch+writePos_,ch);
         readPos_=0;
         writePos_=readPos_+save;
         assert(save == ReadableBytes());
     }
}