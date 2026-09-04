#include "buffer.h"
#include "Auth.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <string>
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
    if (readPos_==writePos_) {
       readPos_=writePos_=0;
    }
}
void Buffer::RetrieveUntil(const char* end){
    assert(Peek()<=end);
    Retrieve(end-Peek());
} 
//读取all，或读到str
void Buffer::RetrieveAll(){
   // bzero(buffer_.data(),buffer_.size());
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
   // char buff[65535-buf_size];数组要常量是因为栈变量的大小要确定，指针和vector底层都是堆空间
    size_t  length=200000-buffer_.size();
    char* buff=new char[length];//设置上限，最大195kb，socket缓冲区的default值是 212992
     iovec iov[2];//2个地方分别是buffer和临时缓冲区，配合readv，writev，read/write依次写满
     const size_t save_writableBytes=WritableBytes();
    iov[0].iov_base=BeginWrite();
    iov[0].iov_len=save_writableBytes;
    iov[1].iov_base=buff;
    iov[1].iov_len=length;

    const ssize_t len=readv(fd, iov,2);
    if(len<0){
        *saveErrno = errno;
    }
    else if(static_cast<size_t>(len)<=save_writableBytes){
         HasWritten(len);
    }
    else  {
        //扩容
        writePos_=buffer_.size();
        Append(buff,len-save_writableBytes);
    }
    delete[]  buff;
    return len;
}
//向fd中写入
ssize_t Buffer::WriteFd(int fd, size_t& len_){
   // size_t save_ReadableBytes=ReadableBytes();
   //len是要传的大小也兼顾已经write的大小
   ssize_t offect=0;
    while (offect<len_) {
    ssize_t tmp=write(fd,Peek()+offect,len_-offect);
    if(tmp>0){
        // readPos_+=tmp;
         offect+=tmp;
    }
    else if (tmp<0&&errno==EINTR) {
        continue;
    }
    //其他错误
    break;
  }
    //2种情况，写完，错误,文件的写入没有缓冲区满
     return offect;
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
    //begin____readpos____writepos____end
    //是个循坏写入的buffer，当0-radpos和writepos-end都小于len时，扩容
     if(WritableBytes()+PrependableBytes()<len){
          buffer_.resize(writePos_+len);
     }
     else {
        char* ch=BeginPtr_();
        size_t save=ReadableBytes();
        //移动到最前方
         std::copy(ch+readPos_,ch+writePos_,ch);
         readPos_=0;
         writePos_=readPos_+save;
         assert(save == ReadableBytes());
     }
}
void Buffer::adjust_pos(){
      if (readPos_==0) {
         return;
      }
      char* ch=BeginPtr_();
        size_t save=ReadableBytes();
        //移动到最前方
         std::copy(ch+readPos_,ch+writePos_,ch);
         readPos_=0;
         writePos_=readPos_+save;
         assert(save == ReadableBytes());
}
std::string::size_type Buffer::find(const std::string& target){
     //find_first_of找的是参数的第一个字符匹配的位置而不是整个string，适合单个
     //find是string，内部大部分表示kmp，一般是暴力
     if (target.empty()) {
        return 0;
    }

    const char* begin = Peek();
    const char* end = BeginWriteConst();

    const char* pos = std::search(
        begin,
        end,
        target.begin(),
        target.end()
    );

    if (pos == end) {
        return std::string::npos;
    }

    return static_cast<size_t>(pos - begin);
}
std::string::size_type Buffer::find(const char* target,int len){
    if (!target) {
        return 0;
    }
   //find(std::string(target,len));
    const char* begin = Peek();
    const char* end = BeginWriteConst();

    const char* pos = std::search(
        begin,
        end,
        target,
        target+len
    );

    if (pos == end) {
        return std::string::npos;
    }

    return static_cast<size_t>(pos - begin);
}
