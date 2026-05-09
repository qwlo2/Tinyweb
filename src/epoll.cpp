#include "epoll.h"
#include "log.h"

Epoller::Epoller(int maxEvent):epollfd(epoll_create(5)),events_(maxEvent){
    assert(epollfd>0&&events_.size()>0);
}

Epoller::~Epoller(){
    close(epollfd);
}
bool Epoller::AddFd(int fd,uint32_t event_){
    if(fd<0){
        LOG_INFO("fd<0");
    return false;
    }
    epoll_event Events;
    Events.data.fd=fd;
    Events.events=event_;
    return 0==epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&Events);

}
bool Epoller::ModFd(int fd,uint32_t event_){
    if(fd<0){
        return false;
    }
     
    epoll_event Events{0};
    Events.data.fd=fd;
    Events.events=event_;
    return 0==epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&Events);
}
bool Epoller::DleFd(int fd){
    if(fd<0)
     return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
}
int Epoller::Wait(int timeoutms){
      return epoll_wait(epollfd,&events_[0],static_cast<int>(events_.size()),timeoutms);
}
int Epoller::GetEventFd(size_t i) const{
    assert(i<events_.size()&&i>=0);
        return events_[i].data.fd;
}
uint32_t Epoller::GetEvent(size_t i) const{
     assert(i<events_.size()&&i>=0); 
    return  events_[i].events;
}