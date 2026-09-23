// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/core.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
namespace rkmoon {
namespace {
[[noreturn]] void fail(const char* s) { throw std::runtime_error(std::string(s)+": "+std::strerror(errno)); }
void put(uint8_t* p,uint64_t v,unsigned n) { for(unsigned i=0;i<n;++i) p[i]=uint8_t(v>>(i*8)); }
uint64_t get(const uint8_t* p,unsigned n) { uint64_t v=0; for(unsigned i=0;i<n;++i) v|=uint64_t(p[i])<<(i*8); return v; }
void io(int fd,uint8_t* data,size_t n,bool writing,uint64_t deadline) {
  while(n) {
    auto now=now_us(); if(now>=deadline) throw std::runtime_error("IPC deadline exceeded");
    pollfd p{fd,short(writing?POLLOUT:POLLIN),0};
    int r=::poll(&p,1,int(std::min<uint64_t>((deadline-now+999)/1000,2147483647)));
    if(r<0&&errno==EINTR) continue;
    if(r<0) fail("poll");
    if(!r) continue;
    if(p.revents&POLLNVAL) throw std::runtime_error("invalid IPC descriptor");
    auto used=writing ? ::send(fd,data,n,MSG_NOSIGNAL|MSG_DONTWAIT) : ::recv(fd,data,n,MSG_DONTWAIT);
    if(used<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)) continue;
    if(used<0) fail(writing?"send":"recv");
    if(!used) throw std::runtime_error("IPC peer closed");
    data+=used; n-=size_t(used);
  }
}
}
void Config::validate() const {
  if(codec!=Codec::h264 && codec!=Codec::hevc) throw std::runtime_error("unsupported codec");
  if(width<64||height<64||width>3840||height>2160||(width&1)||(height&1)) throw std::runtime_error("invalid dimensions");
  if(fps_x100<5900||fps_x100>6010) throw std::runtime_error("v1 only accepts native ~60 Hz");
  if(bitrate<1000000||bitrate>35000000||!gop||gop>120) throw std::runtime_error("invalid bitrate/GOP");
}
uint64_t now_us() {
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::array<uint8_t,header_size> pack(const Header& h) {
  if(h.size>max_au) throw std::runtime_error("oversize AU");
  std::array<uint8_t,header_size> b{}; std::memcpy(b.data(),"RKMF",4);
  put(&b[4],1,2); put(&b[6],uint16_t(h.kind),2); put(&b[8],h.size,4); put(&b[12],h.flags,4);
  put(&b[16],h.seq,8); put(&b[24],h.dequeue_us,8); put(&b[32],h.submit_us,8); put(&b[40],h.done_us,8);
  put(&b[48],h.width,4); put(&b[52],h.height,4); put(&b[56],uint16_t(h.codec),2); put(&b[60],h.extra,4);
  return b;
}
Header unpack(const uint8_t* p,size_t n) {
  if(n!=header_size||std::memcmp(p,"RKMF",4)||get(p+4,2)!=1||get(p+58,2)) throw std::runtime_error("invalid wire version/header");
  Header h; h.kind=Kind(get(p+6,2)); h.size=uint32_t(get(p+8,4)); h.flags=uint32_t(get(p+12,4));
  h.seq=get(p+16,8); h.dequeue_us=get(p+24,8); h.submit_us=get(p+32,8); h.done_us=get(p+40,8);
  h.width=uint32_t(get(p+48,4)); h.height=uint32_t(get(p+52,4)); h.codec=Codec(get(p+56,2)); h.extra=uint32_t(get(p+60,4));
  if(uint16_t(h.kind)<1||uint16_t(h.kind)>8||h.size>max_au||(h.flags&~15U)) throw std::runtime_error("invalid wire fields");
  if(h.kind!=Kind::frame&&h.kind!=Kind::error&&h.size) throw std::runtime_error("unexpected control payload");
  if(h.kind==Kind::error&&h.size>1024) throw std::runtime_error("oversize diagnostic");
  return h;
}
Message receive(int fd,std::chrono::milliseconds timeout) {
  if(timeout.count()<=0) throw std::runtime_error("invalid receive timeout");
  uint64_t end=now_us()+uint64_t(timeout.count())*1000;
  std::array<uint8_t,header_size> b{}; io(fd,b.data(),b.size(),false,end);
  Message m; m.h=unpack(b.data(),b.size()); m.bytes.resize(m.h.size);
  io(fd,m.bytes.data(),m.bytes.size(),false,end); return m;
}
void send(int fd,const Message& m,std::chrono::milliseconds timeout) {
  if(m.h.size!=m.bytes.size()||timeout.count()<=0) throw std::runtime_error("message size/timeout mismatch");
  auto b=pack(m.h); auto end=now_us()+uint64_t(timeout.count())*1000;
  io(fd,b.data(),b.size(),true,end);
  io(fd,const_cast<uint8_t*>(m.bytes.data()),m.bytes.size(),true,end);
}
bool readable(int fd,std::chrono::milliseconds timeout) {
  pollfd p{fd,POLLIN,0}; int r;
  do { r=::poll(&p,1,int(timeout.count())); } while(r<0&&errno==EINTR);
  if(r<0) fail("poll");
  return r>0;
}
Fd::~Fd(){reset();} Fd::Fd(Fd&& o) noexcept:fd_(o.release()){}
Fd& Fd::operator=(Fd&& o) noexcept{if(this!=&o) reset(o.release());return *this;}
int Fd::release(){int v=fd_;fd_=-1;return v;}
void Fd::reset(int f){if(fd_>=0) ::close(fd_);fd_=f;}
Child::Child(const std::string& executable,const std::vector<std::string>& arguments) {
  if(executable.empty()||executable[0]!='/') throw std::runtime_error("worker path must be absolute; no shell/PATH execution");
  int s[2]; if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,s)) fail("socketpair");
  Fd parent(s[0]), child(s[1]);
  // Keep sources above fd 3. dup2 then close the original descriptors in the spawned child.
  Fd out(fcntl(child.get(),F_DUPFD_CLOEXEC,10)); if(out.get()<0) fail("dup socket");
  posix_spawn_file_actions_t actions;
  if(posix_spawn_file_actions_init(&actions)) throw std::runtime_error("spawn actions init");
  // Reserve stdin before dynamic-library constructors run. Sunshine can leave
  // stdin CLOEXEC; pinned MPP incorrectly rejects a successfully opened fd 0.
  int e=posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
  if(!e) e=posix_spawn_file_actions_adddup2(&actions,out.get(),3);
  if(!e) e=posix_spawn_file_actions_addclose(&actions,out.get());
  std::vector<std::string> args{executable,"--ipc-fd","3"}; args.insert(args.end(),arguments.begin(),arguments.end());
  std::vector<char*> argv; for(auto& a:args) argv.push_back(a.data()); argv.push_back(nullptr);
  pid_t pid=-1;
  if(!e) e=posix_spawn(&pid,executable.c_str(),&actions,nullptr,argv.data(),environ);
  posix_spawn_file_actions_destroy(&actions);
  if(e) {errno=e;fail("spawn worker");}
  pid_=pid; socket_=std::move(parent);
}
Child::~Child(){terminate();}
void Child::terminate() {
  if(pid_<0) return;
  socket_.reset(); // EOF is normal stop; a stuck encoder is terminated below.
  for(int i=0;i<50;++i) {
    int status{}; auto r=waitpid(pid_,&status,WNOHANG);
    if(r==pid_||(r<0&&errno==ECHILD)){pid_=-1;return;}
    if(i==10) kill(pid_,SIGTERM);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  kill(pid_,SIGKILL); while(waitpid(pid_,nullptr,0)<0&&errno==EINTR){} pid_=-1;
}
} // namespace rkmoon
