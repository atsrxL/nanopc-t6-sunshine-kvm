// SPDX-License-Identifier: GPL-3.0-or-later
#include "../sunshine/rkmoon_display.hpp"
#include <cassert>
int main(){
  v4l2_dv_timings t{};t.type=V4L2_DV_BT_656_1120;
  auto& b=t.bt;b.width=2560;b.height=1440;b.hfrontporch=48;b.hsync=32;b.hbackporch=80;
  b.vfrontporch=3;b.vsync=5;b.vbackporch=55;b.pixelclock=367930000;
  auto d=rkmoon_sunshine::describe_timing(t,true,true);
  assert(std::strcmp(d.status,"ready")==0&&d.width==2560&&d.height==1440&&d.fps_x100==9000);
  assert(std::strcmp(rkmoon_sunshine::describe_timing(t,true,false).status,"unsupported")==0);
  assert(std::strcmp(rkmoon_sunshine::describe_timing(t,false,true).status,"unsupported")==0);
  b.interlaced=1;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);b.interlaced=0;
  b.pixelclock=0;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  b.width=1920;b.height=1080;b.hfrontporch=88;b.hsync=44;b.hbackporch=148;
  b.vfrontporch=4;b.vsync=5;b.vbackporch=36;b.pixelclock=148500000;
  d=rkmoon_sunshine::describe_timing(t,false,false);assert(d.fps_x100==6000&&d.width==1920);
  b.pixelclock=148350000;d=rkmoon_sunshine::describe_timing(t,false,false);assert(d.fps_x100==5994);
  b.width=UINT32_MAX;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  b.width=0;b.hfrontporch=0;b.hsync=0;b.hbackporch=0;assert(!rkmoon_sunshine::describe_timing(t,true,true).width);
  setenv("RKMOON_VIDEO_DEVICE","/nonexistent-rkmoon-display-test",1);
  d=rkmoon_sunshine::current_display();assert(std::strcmp(d.status,"unavailable")==0&&!d.width&&!d.fps_x100);
}
