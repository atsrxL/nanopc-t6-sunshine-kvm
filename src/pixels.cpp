// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/core.hpp"
#include <algorithm>
#include <cstring>
namespace rkmoon {
bool direct_layout(const Layout& l) {
  uint64_t bpp=l.pixels==Pixels::bgr24?3:1;
  uint64_t size=uint64_t(l.width)*l.height*(l.pixels==Pixels::bgr24?6:3)/2;
  return l.width&&l.height&&!(l.width%64)&&!(l.height%16)&&l.stride==l.width*bpp&&l.sizeimage==size;
}
void to_nv12(const Layout& l,const uint8_t* src,size_t n,uint8_t* dst,size_t dn,uint32_t ds,uint32_t vs) {
  uint64_t row=uint64_t(l.width)*(l.pixels==Pixels::bgr24?3:1);
  uint64_t bytes=uint64_t(l.stride)*l.height*(l.pixels==Pixels::bgr24?2:3)/2;
  uint64_t dest=uint64_t(ds)*vs*3/2;
  if(!src||!dst||!l.width||!l.height||(l.width&1)||(l.height&1)||(ds&1)||(vs&1)||l.stride<row||ds<l.width||vs<l.height||bytes>n||dest>dn) throw std::runtime_error("pixel bounds/stride mismatch");
  if(l.pixels==Pixels::bgr24&&l.full_range) throw std::runtime_error("BGR output range must be explicit limited BT709");
  std::memset(dst,l.full_range?0:16,size_t(ds)*vs); std::memset(dst+size_t(ds)*vs,128,size_t(ds)*vs/2);
  if(l.pixels==Pixels::nv12) {
    for(uint32_t y=0;y<l.height;++y) std::memcpy(dst+size_t(y)*ds,src+size_t(y)*l.stride,l.width);
    for(uint32_t y=0;y<l.height/2;++y) std::memcpy(dst+size_t(ds)*vs+size_t(y)*ds,src+size_t(l.stride)*l.height+size_t(y)*l.stride,l.width);
    return;
  }
  auto clamp=[](int v){return uint8_t(std::clamp(v,0,255));};
  for(uint32_t y=0;y<l.height;y+=2) for(uint32_t x=0;x<l.width;x+=2) {
    int r=0,g=0,b=0;
    for(uint32_t j=0;j<2;++j) for(uint32_t i=0;i<2;++i) {
      const auto* p=src+size_t(y+j)*l.stride+size_t(x+i)*3;
      r+=p[2];g+=p[1];b+=p[0];
      dst[size_t(y+j)*ds+x+i]=clamp(16+((47*p[2]+157*p[1]+16*p[0]+128)>>8));
    }
    r=(r+2)/4;g=(g+2)/4;b=(b+2)/4;
    auto* uv=dst+size_t(ds)*vs+size_t(y/2)*ds+x;
    uv[0]=clamp(128+((-6596*r-22189*g+28785*b+32768)>>16));
    uv[1]=clamp(128+((28785*r-26145*g-2640*b+32768)>>16));
  }
}
} // namespace rkmoon
