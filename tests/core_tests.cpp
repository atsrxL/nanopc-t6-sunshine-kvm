// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic NAL/header fixtures exercise parser contracts, NOT hardware or decoder correctness.
#include "rkmoon/core.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace rkmoon;using namespace std::chrono_literals;
void must(bool v){if(!v)throw std::runtime_error("assertion failed");}
template<class F>void rejects(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}must(threw);}
std::vector<uint8_t> avc(){return {0,0,0,1,0x67,0x42,0,0,1,0x68,0xc0,0,0,1,0x65,0x80};}
std::vector<uint8_t> hevc(){return {0,0,1,0x40,1,0x10,0,0,1,0x42,1,0x10,0,0,1,0x44,1,0x10,0,0,1,0x26,1,0x80};}
Message au(Codec c=Codec::h264){Message m;m.bytes=c==Codec::hevc?hevc():avc();m.h.size=uint32_t(m.bytes.size());m.h.seq=1;m.h.width=1920;m.h.height=1080;m.h.flags=flag_idr;m.h.codec=c;m.h.dequeue_us=10;m.h.submit_us=11;m.h.done_us=20;return m;}
std::pair<Fd,Fd> sockets(){int s[2];must(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,s)==0);return {Fd(s[0]),Fd(s[1])};}
int main(int argc,char** argv){try{
  if(argc>=4&&std::string(argv[1])=="--ipc-fd"){
    Message m;m.h.kind=Kind::caps;m.h.extra=3;send(std::stoi(argv[2]),m,100ms);return 0;
  }
  if(argc==4&&std::string(argv[1])=="--annexb-file"){
    std::ifstream f(argv[3],std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});
    auto n=inspect_annexb(std::string(argv[2])=="hevc"?Codec::hevc:Codec::h264,b);
    std::cout<<"{\"nals\":"<<n.count<<",\"pictures\":"<<n.pictures<<",\"idr\":"<<n.idr<<",\"vps\":"<<n.vps<<",\"sps\":"<<n.sps<<",\"pps\":"<<n.pps<<"}\n";return 0;
  }
  std::map<std::string,std::function<void()>> tests{
    {"wire_roundtrip",[]{auto h=au().h;h.seq=0x8877665544332211ULL;auto b=pack(h);auto d=unpack(b.data(),b.size());must(d.seq==h.seq&&d.size==h.size&&d.width==1920&&b[16]==0x11);}},
    {"wire_oversize",[]{auto h=au().h;h.size=max_au+1;rejects([&]{pack(h);});}},
    {"wire_reserved",[]{auto b=pack(au().h);b[58]=1;rejects([&]{unpack(b.data(),b.size());});}},
    {"wire_kind",[]{auto b=pack(au().h);b[6]=99;rejects([&]{unpack(b.data(),b.size());});}},
    {"wire_payload",[]{auto h=au().h;h.kind=Kind::ack;auto b=pack(h);rejects([&]{unpack(b.data(),b.size());});}},
    {"wire_flags",[]{auto h=au().h;h.flags=32;auto b=pack(h);rejects([&]{unpack(b.data(),b.size());});}},
    {"config_valid",[]{Config{}.validate();}},
    {"config_invalid",[]{Config c;c.width=1919;rejects([&]{c.validate();});c.width=1920;c.fps_x100=3000;rejects([&]{c.validate();});}},
    {"h264_idr",[]{validate_au(au(),true);}},
    {"hevc_idr",[]{validate_au(au(Codec::hevc),true);}},
    {"hevc_cra",[]{auto m=au(Codec::hevc);m.bytes[m.bytes.size()-3]=0x2a;rejects([&]{validate_au(m,true);});}},
    {"missing_headers",[]{auto m=au();m.bytes={0,0,1,0x65,0x80};m.h.size=uint32_t(m.bytes.size());rejects([&]{validate_au(m,true);});}},
    {"two_pictures",[]{auto m=au();m.bytes.insert(m.bytes.end(),{0,0,1,0x41,0x80});m.h.size=uint32_t(m.bytes.size());rejects([&]{validate_au(m,false);});}},
    {"bad_nal",[]{auto m=au(Codec::hevc);m.bytes.back()=0;rejects([&]{validate_au(m,true);});rejects([]{inspect_annexb(Codec::hevc,{0,0,1,0x26,0,0x80});});}},
    {"sequence_bad",[]{SequenceGate g;auto m=au();m.h.seq=2;rejects([&]{g.accept(m);});}},
    {"sequence_good",[]{SequenceGate g;g.accept(au());auto m=au();m.h.seq=2;g.accept(m);}},
    {"pixel_black",[]{Layout l{2,2,6,12,Pixels::bgr24,false};std::vector<uint8_t>s(12),d(6);to_nv12(l,s.data(),s.size(),d.data(),d.size(),2,2);must(d==std::vector<uint8_t>({16,16,16,16,128,128}));}},
    {"pixel_white",[]{Layout l{2,2,6,12,Pixels::bgr24,false};std::vector<uint8_t>s(12,255),d(6);to_nv12(l,s.data(),s.size(),d.data(),d.size(),2,2);must(d==std::vector<uint8_t>({235,235,235,235,128,128}));}},
    {"pixel_stride",[]{Layout l{2,2,4,12,Pixels::nv12,false};std::vector<uint8_t>s{10,20,99,99,30,40,99,99,110,120,99,99},d(24);to_nv12(l,s.data(),s.size(),d.data(),d.size(),4,4);must(d[0]==10&&d[4]==30&&d[8]==16&&d[16]==110&&d[20]==128);}},
    {"pixel_bounds",[]{Layout l{2,2,6,12,Pixels::bgr24,false};uint8_t s[12]{},d[6]{};rejects([&]{to_nv12(l,s,11,d,6,2,2);});rejects([&]{to_nv12(l,s,12,d,5,2,2);});}},
    {"direct_layout",[]{must(direct_layout({3840,2160,3840,12441600,Pixels::nv12,false}));must(!direct_layout({1920,1080,1920,3110400,Pixels::nv12,false}));must(direct_layout({2560,1440,7680,11059200,Pixels::bgr24,false}));}},
    {"ipc_roundtrip",[]{auto s=sockets();auto m=au();send(s.first.get(),m,100ms);auto got=receive(s.second.get(),100ms);must(got.bytes==m.bytes&&got.h.seq==1);}},
    {"ipc_eof",[]{auto s=sockets();s.first.reset();rejects([&]{receive(s.second.get(),100ms);});}},
    {"ipc_deadline",[]{auto s=sockets();auto begin=now_us();rejects([&]{receive(s.second.get(),20ms);});auto elapsed=now_us()-begin;must(elapsed>=15000&&elapsed<500000);}},
    {"child_spawn",[&]{Child child(std::filesystem::absolute(argv[0]).string(),{"--test-child"});auto got=receive(child.fd(),2s);must(got.h.kind==Kind::caps&&got.h.extra==3);}}
  };
  if(argc!=2||!tests.count(argv[1])){std::cerr<<"unknown test\n";return 2;}
  tests.at(argv[1])();std::cout<<"PASS "<<argv[1]<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
