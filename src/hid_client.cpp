// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/hid_client.hpp"
#include <cerrno>
#include <climits>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace rkmoon {
namespace {
void transmit(int fd,const std::string& s){
  size_t offset=0;auto deadline=now_us()+100000;
  while(offset<s.size()){
    if(now_us()>=deadline)throw std::runtime_error("HID send deadline exceeded");
    pollfd p{fd,POLLOUT,0};int r=poll(&p,1,10);
    if(r<0&&errno==EINTR)continue;
    if(r<0)throw std::runtime_error("HID poll error");
    if(!r)continue;
    auto n=::send(fd,s.data()+offset,s.size()-offset,MSG_DONTWAIT|MSG_NOSIGNAL);
    if(n<0&&(errno==EAGAIN||errno==EINTR))continue;
    if(n<=0)throw std::runtime_error("HID connection failed");
    offset+=size_t(n);
  }
}
std::string encode(const HidEvent& e){
  std::string s="{\"op\":\"";
  switch(e.type){
    case HidEvent::key:s+="key\",\"vk\":"+std::to_string(e.a)+",\"down\":"+(e.down?"true":"false");break;
    case HidEvent::button:s+="button\",\"button\":"+std::to_string(e.a)+",\"down\":"+(e.down?"true":"false");break;
    case HidEvent::move:case HidEvent::wheel:s+=(e.type==HidEvent::move?"move":"wheel");s+="\",\"x\":"+std::to_string(e.a)+",\"y\":"+std::to_string(e.b);break;
  }
  return s+"}\n";
}
}
HidClient::HidClient(const std::string& path):fd_(socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0)) {
  sockaddr_un addr{};addr.sun_family=AF_UNIX;
  if(fd_.get()<0||path.empty()||path.size()>=sizeof(addr.sun_path)||path[0]!='/')throw std::runtime_error("invalid HID socket/path");
  std::memcpy(addr.sun_path,path.c_str(),path.size()+1);
  int r=connect(fd_.get(),reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
  if(r<0&&errno!=EINPROGRESS)throw std::runtime_error("HID bridge connect failed");
  if(r<0){pollfd p{fd_.get(),POLLOUT,0};if(poll(&p,1,1000)<=0)throw std::runtime_error("HID connect timeout");}
  int error=0;socklen_t len=sizeof(error);if(getsockopt(fd_.get(),SOL_SOCKET,SO_ERROR,&error,&len)||error)throw std::runtime_error("HID socket error");
  ucred cred{};len=sizeof(cred);
  if(getsockopt(fd_.get(),SOL_SOCKET,SO_PEERCRED,&cred,&len)||cred.uid!=geteuid())throw std::runtime_error("HID server UID mismatch");
  transmit(fd_.get(),"{\"op\":\"hello\",\"version\":1}\n");
  std::string reply;auto end=now_us()+2000000;
  while(now_us()<end&&reply.size()<128){
    if(!readable(fd_.get(),std::chrono::milliseconds(20)))continue;
    char c;auto n=recv(fd_.get(),&c,1,MSG_DONTWAIT);
    if(n<0&&(errno==EINTR||errno==EAGAIN))continue;
    if(n<=0)throw std::runtime_error("HID handshake closed");
    reply+=c;if(c=='\n')break;
  }
  if(reply!="{\"ok\":true}\n")throw std::runtime_error("HID lease refused or handshake timed out");
  thread_=std::thread(&HidClient::run,this);
}
HidClient::~HidClient(){stop_=true;cv_.notify_all();if(thread_.joinable())thread_.join();fd_.reset();}
bool HidClient::submit(HidEvent e){
  if(!healthy_||stop_)return false;
  bool valid=(e.type==HidEvent::key&&e.a>=0&&e.a<=255)||(e.type==HidEvent::button&&e.a>=1&&e.a<=5)||
    ((e.type==HidEvent::move||e.type==HidEvent::wheel)&&e.a>=-32767&&e.a<=32767&&e.b>=-32767&&e.b<=32767);
  if(!valid){healthy_=false;cv_.notify_all();return false;}
  std::lock_guard lock(mutex_);
  if(e.type==HidEvent::move&&!events_.empty()&&events_.back().type==HidEvent::move){
    int x=events_.back().a+e.a,y=events_.back().b+e.b;
    if(x>=-32767&&x<=32767&&y>=-32767&&y<=32767){events_.back().a=x;events_.back().b=y;return true;}
  }
  if(events_.size()>=64){healthy_=false;cv_.notify_all();return false;}
  events_.push_back(e);highwater_=std::max(highwater_,events_.size());cv_.notify_one();return true;
}
size_t HidClient::highwater(){std::lock_guard lock(mutex_);return highwater_;}
void HidClient::run(){
  auto heartbeat=now_us();
  try {
    while(!stop_&&healthy_){
      if(readable(fd_.get(),std::chrono::milliseconds(0))) {
        char c;auto n=recv(fd_.get(),&c,1,MSG_DONTWAIT);
        if(n>=0||errno!=EAGAIN)throw std::runtime_error("HID peer ended/rejected lease");
      }
      HidEvent e{HidEvent::move};bool have=false;
      {std::unique_lock lock(mutex_);cv_.wait_for(lock,std::chrono::milliseconds(20),[&]{return !events_.empty()||stop_||!healthy_;});
       if(!events_.empty()){e=events_.front();events_.pop_front();have=true;}}
      if(stop_||!healthy_)break;
      if(have)transmit(fd_.get(),encode(e));
      if(now_us()-heartbeat>=250000){transmit(fd_.get(),"{\"op\":\"ping\"}\n");heartbeat=now_us();}
    }
  }catch(...){healthy_=false;}
  shutdown(fd_.get(),SHUT_RDWR); // Immediate EOF revokes the Python lease and discards queued stale events.
}
} // namespace rkmoon
