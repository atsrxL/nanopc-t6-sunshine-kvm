// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_bridge.hpp"
#include "rkmoon/core.hpp"
#include "rkmoon/hid_client.hpp"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/utility.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <thread>
#include <unistd.h>
namespace rkmoon_sunshine {
using namespace std::chrono_literals;
namespace {
std::mutex video_owner,input_mutex;
std::shared_ptr<rkmoon::HidClient> input;
std::string env(const char* key,const char* fallback=""){const char* v=getenv(key);return v?v:fallback;}
bool yes(const char* key){return env(key)=="1";}
void forward(rkmoon::HidEvent e){std::shared_ptr<rkmoon::HidClient> p;{std::lock_guard lock(input_mutex);p=input;}if(p)p->submit(e);}
rkmoon::Config translate(const video::config_t& c){
  if(c.rkmoon_absolute_mouse&&!yes("RKMOON_ALLOW_ABSOLUTE_MOUSE"))throw std::runtime_error("absolute mouse not authorized");
  rkmoon::Config out;
  if(c.videoFormat<0||c.videoFormat>1||c.dynamicRange!=0||c.chromaSamplingType!=0||c.enableIntraRefresh)throw std::runtime_error("RKMoon only supports SDR 8-bit 4:2:0 H264/HEVC, IDR recovery");
  if(c.bitrate<1000||c.bitrate>35000)throw std::runtime_error("RKMoon negotiated video bitrate must be 1..35 Mbps");
  out.width=uint32_t(c.width);out.height=uint32_t(c.height);
  if(c.framerateX100<=0 && (c.framerate<59||c.framerate>60) && !(c.framerate==90&&yes("RKMOON_ALLOW_1440P90_EXPERIMENT")))throw std::runtime_error("invalid integer framerate");
  out.fps_x100=uint32_t(c.framerateX100>0?c.framerateX100:c.framerate*100);
  out.bitrate=uint32_t(c.bitrate)*1000;out.codec=c.videoFormat==1?rkmoon::Codec::hevc:rkmoon::Codec::h264;
  if(!yes("RKMOON_ALLOW_HIGH_RES")&&(out.width!=1920||out.height!=1080))throw std::runtime_error("RKMoon first-stage mode is 1920x1080; higher modes require explicit acceptance gate");
  out.allow_1440p90_experiment=yes("RKMOON_ALLOW_1440P90_EXPERIMENT")&&yes("RKMOON_ALLOW_HIGH_RES");
  out.validate();return out;
}
std::vector<std::string> args(const rkmoon::Config& c){
  std::vector<std::string> a{"--device",env("RKMOON_VIDEO_DEVICE","/dev/video0"),"--codec",c.codec==rkmoon::Codec::hevc?"hevc":"h264",
    "--width",std::to_string(c.width),"--height",std::to_string(c.height),"--fps-x100",std::to_string(c.fps_x100),"--bitrate",std::to_string(c.bitrate),"--gop",std::to_string(c.fps_x100>=8900?90:60),"--ack-capture-ownership"};
  if(c.allow_1440p90_experiment&&c.fps_x100>=8900) a.push_back("--allow-1440p90-experiment");
  if(yes("RKMOON_ALLOW_COPY")) a.push_back("--allow-copy");
  return a;
}
void release_input(){std::shared_ptr<rkmoon::HidClient> old;{std::lock_guard lock(input_mutex);old=std::move(input);}old.reset();}
}
bool enabled() noexcept{return true;}
bool request_supported(const video::config_t& c) noexcept{try{translate(c);return yes("RKMOON_CAPTURE_AUTHORIZED");}catch(...){return false;}}
int probe(){
  video::active_hevc_mode=1;video::active_av1_mode=1;
  video::last_encoder_probe_supported_ref_frames_invalidation=false;
  video::last_encoder_probe_supported_yuv444_for_codec.fill(false);
  try {
    if(geteuid()==0)throw std::runtime_error("run dedicated Sunshine unprivileged with device ACLs; do not add file capabilities");
    rkmoon::Child worker(env("RKMOON_WORKER"),{"--probe"});
    auto response=rkmoon::receive(worker.fd(),3000ms);
    if(response.h.kind!=rkmoon::Kind::caps||!(response.h.extra&1)||(response.h.extra&~3U))throw std::runtime_error("hardware H264 probe failed/invalid capability report");
    video::active_hevc_mode=(response.h.extra&2)?2:1;
    BOOST_LOG(info)<<"RKMOON_DEDICATED_BUILD_v1 RKMoon hardware encoder probe succeeded; HEVC="<<(video::active_hevc_mode==2)<<"; this is not HDMI/Moonlight acceptance";
    return 0;
  }catch(const std::exception& e){BOOST_LOG(error)<<"RKMoon probe failed, NO software/desktop fallback: "<<e.what();return -1;}
}
void capture(safe::mail_t mail,video::config_t config,void* channel_data){
  auto shutdown_event=mail->event<bool>(mail::shutdown);
  std::unique_lock owner(video_owner,std::try_to_lock);
  if(!owner.owns_lock()){BOOST_LOG(error)<<"RKMoon exclusive capture busy";shutdown_event->raise(true);return;}
  // An explicit scope ensures input EOF/release happens before this owner can be replaced.
  auto cleanup=util::fail_guard([&]{release_input();shutdown_event->raise(true);});
  try {
    if(!yes("RKMOON_CAPTURE_AUTHORIZED"))throw std::runtime_error("capture ownership has not been granted");
    auto c=translate(config);
    std::shared_ptr<rkmoon::HidClient> pending_input;
    if(!env("RKMOON_HID_SOCKET").empty()) pending_input=std::make_shared<rkmoon::HidClient>(env("RKMOON_HID_SOCKET"),config.rkmoon_absolute_mouse);
    rkmoon::Child worker(env("RKMOON_WORKER"),args(c));
    auto early_release=util::fail_guard([&]{release_input();pending_input.reset();});
    auto ready=rkmoon::receive(worker.fd(),3000ms);
    if(ready.h.kind!=rkmoon::Kind::ready||ready.h.width!=c.width||ready.h.height!=c.height||ready.h.codec!=c.codec||ready.h.extra!=c.fps_x100)throw std::runtime_error("capture negotiation failed");
    // HDMI is the entire input viewport. No T6 desktop layout or logical scaling.
    mail->event<::input::touch_port_t>(mail::touch_port)->raise(::input::touch_port_t{
      {0,0,int(c.width),int(c.height),0,0},int(c.width),int(c.height),0,0,1,1,0,0});
    mail->event<video::hdr_info_t>(mail::hdr)->raise(std::make_unique<video::hdr_info_raw_t>(false));
    {std::lock_guard lock(input_mutex);input=std::move(pending_input);}
    auto idr=mail->event<bool>(mail::idr);
    auto queue=mail::man->queue<video::packet_t>(mail::video_packets);
    rkmoon::SequenceGate gate;
    uint64_t last_progress=rkmoon::now_us(),last_log=last_progress;size_t queue_high=0;
    while(!shutdown_event->peek()) {
      {std::lock_guard lock(input_mutex);if(input&&!input->healthy())throw std::runtime_error("HID lease lost; terminating session");}
      if(idr->peek()) {idr->pop();rkmoon::Message ctl;ctl.h.kind=rkmoon::Kind::idr;rkmoon::send(worker.fd(),ctl,100ms);}
      if(!rkmoon::readable(worker.fd(),20ms)) {
        if(rkmoon::now_us()-last_progress>1500000)throw std::runtime_error("HDMI/worker stalled");
        continue;
      }
      auto frame=rkmoon::receive(worker.fd(),300ms);gate.accept(frame);
      if(frame.h.width!=c.width||frame.h.height!=c.height||frame.h.codec!=c.codec)throw std::runtime_error("frame format epoch mismatch");
      auto now=rkmoon::now_us();
      if(frame.h.done_us>now+1000||frame.h.dequeue_us>now||now-frame.h.dequeue_us>200000)throw std::runtime_error("frame processing age exceeds 200ms budget");
      auto seq=frame.h.seq,dq=frame.h.dequeue_us;
      video::packet_t packet=std::make_unique<video::packet_raw_generic>(std::move(frame.bytes),int64_t(seq),bool(frame.h.flags&rkmoon::flag_idr));
      packet->channel_data=channel_data;
      packet->frame_timestamp=std::chrono::steady_clock::now()-std::chrono::microseconds(now-dq);
      // New locked method is applied ONLY to this producer; it never clears/drops reference packets.
      while(!queue->rkmoon_try_raise(2,std::move(packet))){
        if(shutdown_event->peek()||!queue->rkmoon_running()||rkmoon::now_us()-dq>200000)throw std::runtime_error("video sender backpressure; close instead of dropping reference AUs");
        std::this_thread::sleep_for(1ms);
      }
      queue_high=std::max(queue_high,queue->rkmoon_size());
      rkmoon::Message ack;ack.h.kind=rkmoon::Kind::ack;ack.h.seq=seq;rkmoon::send(worker.fd(),ack,100ms);
      last_progress=rkmoon::now_us();
      if(last_progress-last_log>=1000000){
        BOOST_LOG(info)<<"RKMoon frames="<<seq<<" sender_queue_highwater="<<queue_high<<" dequeue_to_enqueue_us="<<last_progress-dq;
        last_log=last_progress;
      }
    }
  }catch(const std::exception& e){BOOST_LOG(error)<<"RKMoon session ended: "<<e.what();}
}
void key(uint16_t vk,bool release,uint8_t flags){(void)flags;forward({rkmoon::HidEvent::key,int(vk),0,!release});}
void relative(int x,int y){forward({rkmoon::HidEvent::move,x,y,false});}
void absolute(const platf::touch_port_t& port,float x,float y){
  if(port.width<=0||port.height<=0||!std::isfinite(x)||!std::isfinite(y))return;
  forward({rkmoon::HidEvent::absolute,rkmoon::absolute_coordinate(x-port.offset_x,port.width),rkmoon::absolute_coordinate(y-port.offset_y,port.height),false});
}
void button(int number,bool release){forward({rkmoon::HidEvent::button,number,0,!release});}
void scroll(int x,int y){forward({rkmoon::HidEvent::wheel,x,y,false});}
} // namespace rkmoon_sunshine
