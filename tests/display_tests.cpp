// SPDX-License-Identifier: GPL-3.0-or-later
#include "../sunshine/rkmoon_display.hpp"
#include <cassert>
#include <initializer_list>
int main(){
  v4l2_dv_timings t{};t.type=V4L2_DV_BT_656_1120;
  auto& b=t.bt;b.width=2560;b.height=1440;b.hfrontporch=48;b.hsync=32;b.hbackporch=80;
  b.vfrontporch=3;b.vsync=5;b.vbackporch=55;b.pixelclock=367930000;
  auto d=rkmoon_sunshine::describe_timing(t,true,true);
  assert(std::strcmp(d.status,"ready")==0&&d.width==2560&&d.height==1440&&d.fps_x100==9000);
  assert(std::strcmp(rkmoon_sunshine::describe_timing(t,true,false).status,"ready")==0);
  assert(std::strcmp(rkmoon_sunshine::describe_timing(t,false,true).status,"unsupported")==0);
  b.interlaced=1;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);b.interlaced=0;
  b.pixelclock=0;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  b.width=1920;b.height=1080;b.hfrontporch=88;b.hsync=44;b.hbackporch=148;
  b.vfrontporch=4;b.vsync=5;b.vbackporch=36;b.pixelclock=148500000;
  d=rkmoon_sunshine::describe_timing(t,false,false);assert(d.fps_x100==6000&&d.width==1920);
  b.pixelclock=148350000;d=rkmoon_sunshine::describe_timing(t,false,false);assert(d.fps_x100==5994);
  b.width=UINT32_MAX;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  b.width=0;b.hfrontporch=0;b.hsync=0;b.hbackporch=0;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  // Synthetic exact timings: total pixels divisible by 100, exercise custom modes and budgets.
  auto check=[&](unsigned w,unsigned h,unsigned fps,bool accepted){
    t={};t.type=V4L2_DV_BT_656_1120;t.bt.width=w;t.bt.height=h;
    t.bt.hfrontporch=(100-w%100)%100;
    t.bt.pixelclock=uint64_t(w+t.bt.hfrontporch)*h*fps/100;
    auto result=rkmoon_sunshine::describe_timing(t,true,false);
    assert((std::strcmp(result.status,"ready")==0)==accepted);
    if(accepted)assert(result.width==w&&result.height==h&&result.fps_x100==fps);
  };
  check(1680,1050,3000,true);check(2558,1438,12010,true);
  check(64,64,100,true);check(3840,2160,6010,true);
  check(3840,2160,6011,false);check(1920,1080,12011,false);
  check(1920,1080,99,false);check(63,64,6000,false);check(3842,2160,6000,false);
  using rkmoon_sunshine::effective_capture_fps;
  for(auto fps:{5994U,11988U,7550U}){
    rkmoon_sunshine::display_info source{"ready",1920,1080,fps};
    unsigned rounded=(fps+50)/100*100;
    assert(effective_capture_fps(1920,1080,rounded,source)==fps);
    assert(effective_capture_fps(1280,720,rounded,source)==rounded);
    assert(effective_capture_fps(1920,1080,rounded-100,source)==rounded-100);
    assert(effective_capture_fps(1920,1080,7551,source)==7551);
    source.status="unavailable";assert(effective_capture_fps(1920,1080,rounded,source)==rounded);
  }
  setenv("RKMOON_VIDEO_DEVICE","/nonexistent-rkmoon-display-test",1);
  d=rkmoon_sunshine::current_display();assert(std::strcmp(d.status,"unavailable")==0&&!d.width&&!d.fps_x100);
}
