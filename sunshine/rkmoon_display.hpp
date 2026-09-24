// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <mutex>
#include <sys/ioctl.h>
#include <unistd.h>

namespace rkmoon_sunshine {
struct display_info {
  const char* status="unavailable";
  uint32_t width=0,height=0,fps_x100=0;
};
inline uint32_t effective_capture_fps(uint32_t width,uint32_t height,uint32_t requested,const display_info& source) {
  // Only integer transport aliases may normalize. Explicit fractional requests remain exact.
  if(std::strcmp(source.status,"ready")==0&&source.width==width&&source.height==height&&
     requested%100==0&&requested/100==(source.fps_x100+50)/100)return source.fps_x100;
  return requested;
}
inline bool display_flag(const char* name) {
  const char* s=std::getenv(name);return s&&std::strcmp(s,"1")==0;
}
inline display_info describe_timing(const v4l2_dv_timings& t,bool high,bool ninety) {
  (void)ninety; // Legacy flag accepted; no longer a mode gate.
  display_info d;d.status="unsupported";
  if(t.type!=V4L2_DV_BT_656_1120||t.bt.interlaced)return d;
  const auto& b=t.bt;
  uint64_t totalw=uint64_t(b.width)+b.hfrontporch+b.hsync+b.hbackporch;
  uint64_t totalh=uint64_t(b.height)+b.vfrontporch+b.vsync+b.vbackporch;
  if(!totalw||!totalh||totalw>100000||totalh>100000||b.pixelclock>10000000000ULL)return d;
  uint64_t total=totalw*totalh;uint64_t fps=(b.pixelclock*100+total/2)/total;
  if(b.width<64||b.width>3840||b.height<64||b.height>2160||(b.width&1)||(b.height&1))return d;
  if(!high&&(b.width!=1920||b.height!=1080))return d;
  if(fps<100||fps>12010||uint64_t(b.width)*b.height*fps>3840ULL*2160*6010)return d;
  d.status="ready";d.width=b.width;d.height=b.height;d.fps_x100=uint32_t(fps);return d;
}
inline display_info current_display() {
  // Never G_FMT here: the vendor driver mutates its active DMA layout in G_FMT.
  // QUERY_DV_TIMINGS alone is safe to poll alongside the exclusive capture worker.
  static std::mutex mutex;static display_info cached;
  static auto checked=std::chrono::steady_clock::time_point{};
  std::lock_guard<std::mutex> lock(mutex);
  auto now=std::chrono::steady_clock::now();
  if(now-checked<std::chrono::milliseconds(250))return cached;
  checked=now;cached={};
  const char* path=std::getenv("RKMOON_VIDEO_DEVICE");
  int fd=::open(path?path:"/dev/video0",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
  if(fd<0)return cached;
  v4l2_dv_timings timing{};int result;
  do{result=::ioctl(fd,VIDIOC_QUERY_DV_TIMINGS,&timing);}while(result<0&&errno==EINTR);
  int error=errno;::close(fd);
  if(result<0){if(error==ENOLINK||error==ENOLCK||error==ENODATA||error==ENODEV)cached.status="no_signal";return cached;}
  cached=describe_timing(timing,display_flag("RKMOON_ALLOW_HIGH_RES"),display_flag("RKMOON_ALLOW_1440P90_EXPERIMENT"));
  return cached;
}
template<class Tree> void put_display_info(Tree& tree) {
  tree.put("root.RKMoonMouseModes",display_flag("RKMOON_ALLOW_ABSOLUTE_MOUSE")?"relative,absolute":"relative");
  auto d=current_display();tree.put("root.RKMoonDisplayVersion",1);
  tree.put("root.RKMoonDisplayStatus",d.status);
  tree.put("root.RKMoonDisplayWidth",d.width);
  tree.put("root.RKMoonDisplayHeight",d.height);
  tree.put("root.RKMoonDisplayFpsX100",d.fps_x100);
}
}
