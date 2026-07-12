#include "log.h"
Log::Log(){
     lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
   // isOpen_=true;
}
 Log::~Log(){
    if(writeThread_&&writeThread_->joinable()){
        while (!deque_->empty()) {
            deque_->flush();
        }
         deque_->Close();
         writeThread_->join();
    }
    if(fp_){
         std::lock_guard<std::mutex> lock(mutex_);
        flush();
        fclose(fp_);
    }

 }
void Log::init(int level=1, const char* path , 
                const char* suffix ,
                int maxQueueCapacity ){
        level_=level;
        isOpen_=true;
        if(maxQueueCapacity>0){
            if(!deque_){
            isAsync_=true;
            deque_=std::make_unique<BlockDeque<std::string>>(maxQueueCapacity);
            writeThread_=std::make_unique<std::thread>(FlushLogThread);//一次分配
            }
        }

        lineCount_=0;
        time_t time=::time(nullptr);
        tm *systime=localtime(&time);//内部是静态缓存，多线程不安全，但这里只有log、
         tm t=*systime;
        path_=path;
        suffix_=suffix;
        char fileName[LOG_NAME_LEN]={0};
         snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
         //year从1900，mon从0开始计数，将格式化后的字符串写入指定缓冲区
        toDay_=t.tm_mday;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(fp_){
                flush();
                fclose(fp_);//先刷新缓存区在关闭
            }
            fp_=fopen(fileName,"a");
            if(fp_==nullptr){
                //filename不存在
            mkdir(path,0777);//0777所以都可以读写
            fp_=fopen(fileName,"a");
            }
            assert(fp_ != nullptr);
        }
    }
void Log::write(int level, const char *format,...){//可变参数函数,format代表最后应该固定参数，此时有int和const char*2个固定参数（命名为format是习惯）
      timeval time={0,0};
      gettimeofday(&time,nullptr);//与time相比，多了微秒，适用于高性能
      time_t tsec=time.tv_sec;
      tm* systime=localtime(&tsec);
      tm t=*systime;
       va_list vaList;
        if(toDay_!=t.tm_mday||(lineCount_&&(lineCount_%MAX_LINES==0))){//第一个lineCount_是为了防止为0时
            std::unique_lock<std::mutex> lock(mutex_);//获取最新值
               lock.unlock();
               char Filename[LOG_NAME_LEN]={0};
               char tail[36]={0};
               snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
               //不同天
               if(toDay_!=t.tm_mday){
                   snprintf(Filename,LOG_PATH_LEN-72,"%s%s%s",path_,tail,suffix_);
                   toDay_=t.tm_mday;
                   lineCount_=0;
               }
               //同天满文件
               else {
                   snprintf(Filename, LOG_NAME_LEN,"%s%s-%d%s",path_,tail,lineCount_/MAX_LINES,suffix_ );
               }
                lock.lock();
                    flush();
                   fclose(fp_);
                   fp_=fopen(Filename,"a");
                   assert(fp_ != nullptr);
        } 
     {
          std::unique_lock<std::mutex> lock(mutex_);
          lineCount_++;
           int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, time.tv_usec);
            buff_.HasWritten(n);
            AppendLogLevelTitle_(level_);
            va_start(vaList,format);
            int m =vsnprintf(buff_.BeginWrite(),buff_.WritableBytes(),format, vaList);
            va_end(vaList);
            buff_.HasWritten(m);
            buff_.Append("\n\0", 2);
            //线程，异步
            if(isAsync_&&deque_&&!deque_->full()){
                deque_->push_back(buff_.RetrieveAllToStr());
          
            }
            //同步
            else {
               fputs(buff_.Peek(),fp_);
            }
            buff_.RetrieveAll();
     }

}
void Log::AppendLogLevelTitle_(int level){
    switch (level) {
      case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}
 Log* Log::Instance(){
    static Log log;
    return &log;
    //单例模式，保证log唯一
}
void Log::FlushLogThread(){
      Log::Instance()->AsyncWrite_();
}
void Log::flush(){
     if(isAsync_){
            deque_->flush();
           }
    fflush(fp_);
}

int Log:: GetLevel(){
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}
void Log::SetLevel(int level){
    std::lock_guard<std::mutex> lock(mutex_);
    level_=level;
   
}

void Log::AsyncWrite_(){
    std::string str="";
    //一直返回true，除close
    while (deque_->pop(str)) {
          std::lock_guard<std::mutex> locker(mutex_);
          fputs(str.c_str(),fp_);
    }
    //log的thread是消费者
}