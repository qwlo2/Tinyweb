#pragma  once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <semaphore.h>
#include <string>
#include <hiredis/hiredis.h>  // redisContext、redisReply、redisCommand
class Session{
   private:
        std::shared_ptr<redisContext> GetConn();
    void FreeConn(redisContext* conn);
    int GetFreeConnCount();

    void Init(const char* host, int port, int connSize);//datebase
    void ClosePool();
   public:
      std::string getUser_id(const std::string& uesename);

      std::optional<std::string> gettoken( const std::string& uesename);

      bool versityToken(const std::string& cookies,size_t& user_id);

      static Session*  Intense();
      Session();
      ~Session();

    int MAX_CONN_;
    //冗余
    int useCount_;
    int freeCount_;
    bool is_inited{false};
    std::queue<redisContext*> connQue_;
    std::mutex mutex_;
    sem_t semId_;
};