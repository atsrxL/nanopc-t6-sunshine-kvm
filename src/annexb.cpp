// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon/core.hpp"
namespace rkmoon {
namespace {
size_t prefix(const std::vector<uint8_t>& b,size_t i) {
  if(i+3<=b.size()&&b[i]==0&&b[i+1]==0&&b[i+2]==1) return 3;
  if(i+4<=b.size()&&b[i]==0&&b[i+1]==0&&b[i+2]==0&&b[i+3]==1) return 4;
  return 0;
}
}
Nals inspect_annexb(Codec c,const std::vector<uint8_t>& b) {
  if(c!=Codec::h264&&c!=Codec::hevc) throw std::runtime_error("unknown AU codec");
  if(b.empty()||b.size()>max_au||!prefix(b,0)) throw std::runtime_error("not bounded Annex-B");
  Nals n;
  for(size_t i=0;i<b.size();) {
    auto p=prefix(b,i); if(!p) throw std::runtime_error("invalid NAL prefix");
    size_t start=i+p,end=start;
    while(end<b.size()&&!prefix(b,end)) ++end;
    size_t need=c==Codec::hevc?2:1;
    if(end-start<need||(b[start]&0x80)) throw std::runtime_error("invalid/truncated NAL header");
    unsigned t=c==Codec::hevc?(b[start]>>1)&63:b[start]&31;
    if(c==Codec::hevc) {
      if((b[start]&1)|| (b[start+1]>>3) || !(b[start+1]&7)) throw std::runtime_error("nonzero HEVC layer or zero temporal_id_plus1");
      n.vps|=t==32; n.sps|=t==33; n.pps|=t==34; n.idr|=t==19||t==20;
      if(t<=31) { n.vcl=true; if(end-start<3) throw std::runtime_error("truncated HEVC slice"); n.pictures+=bool(b[start+2]&0x80); }
      if(t>=16&&t<=23&&t!=19&&t!=20) throw std::runtime_error("v1 requires closed IDR, not CRA/BLA recovery");
    } else {
      if(!t||t>=24) throw std::runtime_error("unsupported H264 NAL type");
      n.sps|=t==7; n.pps|=t==8; n.idr|=t==5;
      if(t==1||t==5) { n.vcl=true; if(end-start<2) throw std::runtime_error("truncated H264 slice"); n.pictures+=bool(b[start+1]&0x80); }
      if(t>=2&&t<=4) throw std::runtime_error("H264 data partitioning unsupported");
    }
    ++n.count; i=end;
  }
  return n;
}
void validate_au(const Message& m,bool require_idr) {
  const auto& h=m.h;
  if(h.kind!=Kind::frame||h.size!=m.bytes.size()||!h.seq||h.width<64||h.width>3840||h.height<64||h.height>2160||(h.width&1)||(h.height&1)) throw std::runtime_error("invalid AU metadata");
  if(h.dequeue_us>h.submit_us||h.submit_us>h.done_us) throw std::runtime_error("nonmonotonic processing timestamps");
  auto n=inspect_annexb(h.codec,m.bytes);
  if(!n.vcl||n.pictures!=1||n.idr!=bool(h.flags&flag_idr)) throw std::runtime_error("AU picture/IDR mismatch");
  if(require_idr&&!n.idr) throw std::runtime_error("expected IDR recovery");
  if(n.idr&&(!n.sps||!n.pps||(h.codec==Codec::hevc&&!n.vps))) throw std::runtime_error("IDR missing inline parameter sets");
}
void SequenceGate::accept(const Message& m) {
  validate_au(m,need_idr_);
  if(m.h.seq!=last_+1) throw std::runtime_error("AU sequence discontinuity; close session, do not send dependent P frames");
  last_=m.h.seq; need_idr_=false;
}
} // namespace rkmoon
