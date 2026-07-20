#pragma once
#include <sys/epoll.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unistd.h>
#include <cassert>
class Epoller{
     public:
      Epoller(int maxEvent = 4096);
      ~Epoller();

      bool AddFd(int fd,uint32_t event_);
      bool ModFd(int fd,uint32_t event_);
      bool DleFd(int fd);
      
      int Wait(int timoutms=-1);
      int GetEventFd(size_t i) const;
      uint32_t GetEvent(size_t i) const;
     private:
     int epollfd;
    std::vector<struct epoll_event> events_; 
};