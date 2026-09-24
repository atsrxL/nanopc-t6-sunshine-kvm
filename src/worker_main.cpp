// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/encoder.hpp"
#include <charconv>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/prctl.h>
#include <unistd.h>
namespace {
volatile sig_atomic_t stopping=0;void stop(int){stopping=1;}
uint32_t number(const std::string& s){uint32_t n{};auto r=std::from_chars(s.data(),s.data()+s.size(),n);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())throw std::runtime_error("invalid numeric argument");return n;}
void write_file(int fd,const std::vector<uint8_t>& data){size_t at=0;while(at<data.size()){auto n=write(fd,data.data()+at,data.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("output file write failed");at+=size_t(n);}}
}
int main(int argc,char** argv){
  using namespace rkmoon;using namespace std::chrono_literals;
  signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
  Config c;std::string device="/dev/video0",output,stats;uint32_t seconds=60,idr_at=30;int ipc=-1;
  bool probe=false,allow_copy=false,authorized=false;
  try {
    for(int i=1;i<argc;++i){std::string k=argv[i];
      if(k=="--help"){std::cout<<"rkmoon-worker --device /dev/videoN --codec hevc|h264 --width 1920 --height 1080 --fps-x100 6000 --bitrate 20000000 --gop 60 --seconds 60 --output NEW_FILE --stats NEW_CSV --ack-capture-ownership [--allow-copy]\n1440p90 experiment only: --width 2560 --height 1440 --fps-x100 9000 --allow-1440p90-experiment\nHardware-only capability probe: --probe (no HDMI capture; emits synthetic black into MPP)\nInternal: --ipc-fd FD; one AU per ACK, K/bitrate/stop control messages\n";return 0;}
      if(k=="--probe"){probe=true;continue;}if(k=="--allow-copy"){allow_copy=true;continue;}if(k=="--ack-capture-ownership"){authorized=true;continue;}
      if(k=="--allow-1440p90-experiment"){c.allow_1440p90_experiment=true;continue;}
      if(++i>=argc)throw std::runtime_error("missing option value");
      std::string v=argv[i];
      if(k=="--device")device=v;else if(k=="--output")output=v;else if(k=="--stats")stats=v;
      else if(k=="--width")c.width=number(v);else if(k=="--height")c.height=number(v);else if(k=="--fps-x100")c.fps_x100=number(v);
      else if(k=="--bitrate")c.bitrate=number(v);else if(k=="--gop")c.gop=number(v);else if(k=="--seconds")seconds=number(v);
      else if(k=="--idr-at")idr_at=number(v);else if(k=="--ipc-fd")ipc=int(number(v));
      else if(k=="--codec"){if(v=="hevc")c.codec=Codec::hevc;else if(v=="h264")c.codec=Codec::h264;else throw std::runtime_error("unknown codec");}
      else throw std::runtime_error("unknown argument");
    }
    c.validate();if(seconds>86400||(!seconds&&ipc<0))throw std::runtime_error("invalid duration");
    if(ipc>=0){auto parent=getppid();if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)throw std::runtime_error("worker parent lost");}
    if(probe){
      uint32_t caps=0;
      for(auto codec:{Codec::h264,Codec::hevc}) {
        try {Config pc=c;pc.codec=codec;Layout l{pc.width,pc.height,pc.width,pc.width*pc.height*3/2,Pixels::nv12,false};
          Encoder enc;enc.open(pc,l,nullptr,true);Capture::Frame f{};f.dequeue_us=now_us();auto au=enc.encode(f,nullptr,true);validate_au(au,true);caps|=codec==Codec::h264?1:2;
        }catch(const std::exception& e){std::cerr<<"hardware probe failed: "<<e.what()<<'\n';}
      }
      if(ipc>=0){Message m;m.h.kind=Kind::caps;m.h.extra=caps;send(ipc,m,2s);}
      std::cout<<"{\"hardware_encoder_probe\":true,\"synthetic_input\":true,\"codec_mask\":"<<caps<<"}\n";
      return caps&1?0:1;
    }
    if(!authorized)throw std::runtime_error("capture ownership not authorized; no device opened");
    if(ipc<0&&output.empty())throw std::runtime_error("standalone mode requires --output");
    Fd file;if(!output.empty()){file.reset(open(output.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));if(file.get()<0)throw std::runtime_error("output already exists or cannot be created");}
    Fd statfd;if(!stats.empty()){statfd.reset(open(stats.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600));if(statfd.get()<0)throw std::runtime_error("stats already exist/cannot create");}
    auto emit=[&](const std::string& s){if(statfd.get()>=0)write_file(statfd.get(),std::vector<uint8_t>(s.begin(),s.end()));};
    emit("seq,driver_seq,driver_timestamp_us,driver_timestamp_flags,dequeue_us,submit_us,done_us,bytes,idr,raw_skipped,dmabuf\n");
    Capture cap(device);cap.describe();cap.validate(c);cap.start(direct_layout(cap.layout));
    // Destruction order is intentional: enc dies before cap on all exits, including exceptions.
    Encoder enc;enc.open(c,cap.layout,&cap.buffers(),allow_copy);
    if(ipc>=0){Message m;m.h.kind=Kind::ready;m.h.width=c.width;m.h.height=c.height;m.h.codec=c.codec;m.h.extra=c.fps_x100;send(ipc,m,2s);}
    auto start=now_us(),last_check=start;bool force=true,sent_test_idr=false;uint64_t count=0;
    while(!stopping&&(ipc>=0||now_us()-start<uint64_t(seconds)*1000000)){
      if(ipc>=0){ // Drain queued controls BEFORE picking the next raw frame.
        while(readable(ipc,0ms)){
          auto ctl=receive(ipc,100ms);
          if(ctl.h.kind==Kind::stop)return 0;
          if(ctl.h.kind==Kind::idr)force=true;
          else if(ctl.h.kind==Kind::bitrate){enc.bitrate(ctl.h.extra);force=true;}
          else throw std::runtime_error("unexpected control before frame");
        }
      }
      if(!sent_test_idr&&idr_at&&now_us()-start>=uint64_t(idr_at)*1000000){force=true;sent_test_idr=true;}
      auto raw=cap.latest(1000ms);auto frame=enc.encode(raw,&cap.buffer(raw.index),force);force=false;
      cap.release(raw.index); // ONLY after full AU completion, never on encoder error.
      ++count;if(file.get()>=0)write_file(file.get(),frame.bytes);
      auto& h=frame.h;emit(std::to_string(h.seq)+","+std::to_string(raw.sequence)+","+std::to_string(raw.driver_us)+","+std::to_string(raw.flags&V4L2_BUF_FLAG_TIMESTAMP_MASK)+","+std::to_string(h.dequeue_us)+","+std::to_string(h.submit_us)+","+std::to_string(h.done_us)+","+std::to_string(h.size)+","+std::to_string(bool(h.flags&flag_idr))+","+std::to_string(cap.raw_skipped)+","+std::to_string(enc.direct())+"\n");
      if(ipc>=0){
        send(ipc,frame,300ms);
        auto deadline=now_us()+2000000;bool ack=false;
        while(!ack&&!stopping){
          auto now=now_us();if(now>=deadline)throw std::runtime_error("AU credit timeout");
          auto ctl=receive(ipc,std::chrono::milliseconds((deadline-now+999)/1000));
          switch(ctl.h.kind){
            case Kind::ack:if(ctl.h.seq!=h.seq)throw std::runtime_error("ACK sequence mismatch");ack=true;break;
            case Kind::idr:force=true;break;
            case Kind::bitrate:enc.bitrate(ctl.h.extra);force=true;break;
            case Kind::stop:return 0;
            default:throw std::runtime_error("unexpected worker control");
          }
        }
      }
      if(now_us()-last_check>=1000000){
        cap.unchanged();last_check=now_us();
        std::cerr<<"{\"kind\":\"stats\",\"encoded\":"<<count<<",\"raw_skipped\":"<<cap.raw_skipped<<",\"elapsed_us\":"<<last_check-start<<",\"dequeue_to_au_us\":"<<h.done_us-h.dequeue_us<<",\"au_bytes\":"<<h.size<<"}\n";
      }
    }
    return 0;
  }catch(const std::exception& e){std::cerr<<"rkmoon-worker: "<<e.what()<<'\n';return 75;}
}
