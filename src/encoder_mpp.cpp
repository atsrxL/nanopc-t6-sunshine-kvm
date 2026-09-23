// SPDX-License-Identifier: GPL-3.0-or-later
// Hardware-only encoder; no FFmpeg software codec dependency or fallback.
#include "rkmoon/encoder.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <thread>
extern "C" {
#include <rk_mpi.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_venc_cfg.h>
}
namespace rkmoon {
namespace {
void ok(MPP_RET v,const char* call){if(v!=MPP_OK)throw std::runtime_error(std::string("MPP ")+call+" failed ("+std::to_string(v)+")");}
uint32_t align64(uint32_t n){return (n+63)&~63U;}
}
struct Encoder::Impl {
  Config c;Layout source;bool direct=false;uint32_t hs{},vs{};uint64_t seq=0;
  MppCtx ctx=nullptr;MppApi* api=nullptr;MppEncCfg cfg=nullptr;
  MppBufferGroup group=nullptr;MppBuffer staging=nullptr;std::vector<MppBuffer> imported;
  MppFrameFormat format=MPP_FMT_YUV420SP;
  std::vector<uint8_t> rgb_scratch; // cached sequential copy before limited RGB CSC
  ~Impl(){
    // Critical ordering: flush/destroy hardware BEFORE imported buffers, then Capture teardown.
    if(ctx) {api->reset(ctx);mpp_destroy(ctx);}
    for(auto b:imported) if(b)mpp_buffer_put(b);
    if(staging)mpp_buffer_put(staging);
    if(group)mpp_buffer_group_put(group);
    if(cfg)mpp_enc_cfg_deinit(cfg);
  }
  void s(const char* k,int32_t v){ok(mpp_enc_cfg_set_s32(cfg,k,v),k);}
  void u(const char* k,uint32_t v){ok(mpp_enc_cfg_set_u32(cfg,k,v),k);}
  void set_rate(uint32_t bps){c.bitrate=bps;c.validate();s("rc:bps_target",int32_t(bps));s("rc:bps_min",int32_t(bps*15/16));s("rc:bps_max",int32_t(bps*17/16));}
};
Encoder::Encoder()=default;Encoder::~Encoder()=default;
bool Encoder::direct() const{return p_&&p_->direct;}
void Encoder::open(const Config& c,const Layout& l,const std::vector<Capture::Buffer>* bufs,bool allow_copy) {
  if(p_)throw std::runtime_error("encoder already open");
  c.validate();
  auto p=std::make_unique<Impl>();p->c=c;p->source=l;
  p->direct=bufs&&direct_layout(l);
  if(p->direct) {
    for(const auto& b:*bufs) {
      MppBuffer imported=nullptr;
      if(b.dma.get()<0){p->direct=false;break;}
      MppBufferInfo info{};info.type=MPP_BUFFER_TYPE_EXT_DMA;info.size=b.size;info.fd=b.dma.get();
      if(mpp_buffer_import(&imported,&info)!=MPP_OK){p->direct=false;break;}
      p->imported.push_back(imported);
    }
    if(!p->direct){for(auto b:p->imported)mpp_buffer_put(b);p->imported.clear();}
  }
  if(!p->direct&&!allow_copy) throw std::runtime_error("DMA-BUF layout/export/import unavailable; explicitly authorize --allow-copy");
  const bool hardware_bgr=l.pixels==Pixels::bgr24&&!l.rgb_limited;
  p->hs=p->direct?l.stride:align64(c.width*(hardware_bgr?3:1));
  p->vs=p->direct?c.height:align64(c.height);
  p->format=hardware_bgr?MPP_FMT_BGR888:MPP_FMT_YUV420SP;
  if(l.pixels==Pixels::bgr24&&l.rgb_limited) p->rgb_scratch.resize(l.sizeimage);
  auto codec=c.codec==Codec::hevc?MPP_VIDEO_CodingHEVC:MPP_VIDEO_CodingAVC;
  ok(mpp_check_support_format(MPP_CTX_ENC,codec),"check_support_format");
  ok(mpp_create(&p->ctx,&p->api),"create");ok(mpp_init(p->ctx,MPP_CTX_ENC,codec),"init");
  ok(mpp_enc_cfg_init(&p->cfg),"cfg_init");ok(p->api->control(p->ctx,MPP_ENC_GET_CFG,p->cfg),"get_cfg");
  p->s("codec:type",codec);p->s("base:low_delay",1);
  p->s("prep:width",int32_t(c.width));p->s("prep:height",int32_t(c.height));
  p->s("prep:hor_stride",int32_t(p->hs));p->s("prep:ver_stride",int32_t(p->vs));p->s("prep:format",p->format);
  // VEPU580 treats RGB input as full range automatically. prep:range selects
  // the RGB-to-YUV output matrix AND the H264/HEVC VUI; range_out is JPEG-only
  // in this pinned SDK. Keep encoded range aligned with our frame metadata.
  p->s("prep:range",l.full_range?MPP_FRAME_RANGE_JPEG:MPP_FRAME_RANGE_MPEG);
  p->s("prep:range_out",l.full_range?MPP_FRAME_RANGE_JPEG:MPP_FRAME_RANGE_MPEG);
  p->s("prep:colorspace",1);p->s("prep:colorprim",1);p->s("prep:colortrc",int32_t(l.transfer));
  p->s("rc:mode",MPP_ENC_RC_MODE_CBR);p->set_rate(c.bitrate);
  p->s("rc:fps_in_flex",0);p->s("rc:fps_in_num",int32_t(c.fps_x100));p->s("rc:fps_in_denom",100);
  p->s("rc:fps_out_flex",0);p->s("rc:fps_out_num",int32_t(c.fps_x100));p->s("rc:fps_out_denom",100);
  p->s("rc:gop",int32_t(c.gop));p->u("rc:drop_mode",MPP_ENC_RC_DROP_FRM_DISABLED);
  p->u("rc:max_reenc_times",0);p->s("rc:qp_init",-1);p->s("rc:qp_min",10);p->s("rc:qp_max",51);
  p->s("rc:qp_min_i",10);p->s("rc:qp_max_i",51);p->s("rc:qp_ip",2);
  p->u("split:mode",0); // No slice-fragment output policy. EOI handling remains defensive.
  if(c.codec==Codec::hevc){
    p->s("h265:profile",1);p->s("h265:tier",0);
    // Main tier: Level 4.1 for 1080p60 (<=20 Mbps); Level 5/5.1 for higher rate/resolution.
    int level=(uint64_t(c.width)*c.height>3686400||c.bitrate>25000000)?153:
               (uint64_t(c.width)*c.height>2228224||c.bitrate>20000000)?150:123;
    p->s("h265:level",level);
  } else {
    // Baseline, no CABAC/B slices, robust compatibility fallback.
    p->s("h264:profile",66);p->s("h264:cabac_en",0);p->s("h264:stream_type",0);p->u("h264:vui_en",1);
    auto mbps=uint64_t((c.width+15)/16)*((c.height+15)/16)*60;
    p->s("h264:level",mbps>983040?52:mbps>522240?51:42);
  }
  ok(p->api->control(p->ctx,MPP_ENC_SET_CFG,p->cfg),"set_cfg");
  MppEncHeaderMode mode=MPP_ENC_HEADER_MODE_EACH_IDR;
  ok(p->api->control(p->ctx,MPP_ENC_SET_HEADER_MODE,&mode),"inline headers");
  MppPollType wait=MPP_POLL_NON_BLOCK;
  ok(p->api->control(p->ctx,MPP_SET_OUTPUT_TIMEOUT,&wait),"nonblocking output");
  if(!p->direct){
    ok(mpp_buffer_group_get_internal(&p->group,MppBufferType(MPP_BUFFER_TYPE_DRM|MPP_BUFFER_FLAGS_CACHABLE),0),"buffer_group");
    const size_t bytes=size_t(p->hs)*p->vs*(hardware_bgr?2:3)/2;
    ok(mpp_buffer_get(p->group,&p->staging,bytes),"staging buffer");
  }
  std::cerr<<"{\"kind\":\"encoder\",\"codec\":\""<<(c.codec==Codec::hevc?"hevc":"h264")
    <<"\",\"hardware\":true,\"path\":\""<<(p->direct?"dmabuf-import":"explicit-cpu-pixel-copy")<<"\"}\n";
  p_=std::move(p);
}
void Encoder::bitrate(uint32_t bps) {
  // Dynamic changes remain within the initially signalled level budget. Higher levels require reconnect.
  if(!p_||bps>p_->c.bitrate) throw std::runtime_error("live bitrate increase requires reconnect in v1");
  p_->set_rate(bps);ok(p_->api->control(p_->ctx,MPP_ENC_SET_CFG,p_->cfg),"change bitrate");
}
Message Encoder::encode(const Capture::Frame& raw,Capture::Buffer* capture,bool force) {
  auto& p=*p_;MppBuffer input=p.direct?p.imported.at(raw.index):p.staging;
  if(p.direct&&(!capture||raw.offset))throw std::runtime_error("nonzero DMABUF offset unsupported");
  if(!p.direct){
    ok(mpp_buffer_sync_begin(input),"staging sync begin");
    auto* dst=static_cast<uint8_t*>(mpp_buffer_get_ptr(input));
    try {
      if(capture){
        bool sync=capture->dma.get()>=0;
        dma_buf_sync fence{DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ};
        if(sync&&ioctl(capture->dma.get(),DMA_BUF_IOCTL_SYNC,&fence)<0)throw std::runtime_error("capture DMA CPU sync failed");
        try {
          if(raw.bytes<raw.offset||raw.bytes-raw.offset<p.source.sizeimage)
            throw std::runtime_error("short capture input");
          const auto* src=static_cast<uint8_t*>(capture->data)+raw.offset;
          if(p.format==MPP_FMT_BGR888){
            // Preserve BGR for MPP's hardware color conversion. Sequential
            // copies avoid expensive repeated reads of uncached V4L2 MMAP.
            std::memset(dst,0,size_t(p.hs)*p.vs);
            for(uint32_t y=0;y<p.c.height;++y)
              std::memcpy(dst+size_t(y)*p.hs,src+size_t(y)*p.source.stride,size_t(p.c.width)*3);
          }else{
            if(!p.rgb_scratch.empty()) {
              std::memcpy(p.rgb_scratch.data(),src,p.source.sizeimage);
              src=p.rgb_scratch.data();
            }
            to_nv12(p.source,src,p.rgb_scratch.empty()?raw.bytes-raw.offset:p.rgb_scratch.size(),dst,size_t(p.hs)*p.vs*3/2,p.hs,p.vs);
          }
        }
        catch(...){if(sync){fence.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;ioctl(capture->dma.get(),DMA_BUF_IOCTL_SYNC,&fence);}throw;}
        if(sync){fence.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;if(ioctl(capture->dma.get(),DMA_BUF_IOCTL_SYNC,&fence)<0)throw std::runtime_error("capture DMA CPU sync end failed");}
      } else { // Hardware encoder capability probe ONLY; never substituted into a live HDMI stream.
        std::memset(dst,p.source.full_range?0:16,size_t(p.hs)*p.vs);std::memset(dst+size_t(p.hs)*p.vs,128,size_t(p.hs)*p.vs/2);
      }
    }catch(...){mpp_buffer_sync_end(input);throw;}
    ok(mpp_buffer_sync_end(input),"staging sync end");
  }
  if(force)ok(p.api->control(p.ctx,MPP_ENC_SET_IDR_FRAME,nullptr),"request IDR");
  Message m;m.h.kind=Kind::frame;m.h.seq=++p.seq;m.h.codec=p.c.codec;m.h.width=p.c.width;m.h.height=p.c.height;
  m.h.dequeue_us=raw.dequeue_us;m.h.submit_us=now_us();
  m.h.flags=flag_bt709|(p.source.full_range?uint32_t(flag_full_range):0U)|(p.direct?uint32_t(flag_dmabuf):0U);
  MppFrame f=nullptr;ok(mpp_frame_init(&f),"frame_init");
  mpp_frame_set_width(f,p.c.width);mpp_frame_set_height(f,p.c.height);mpp_frame_set_hor_stride(f,p.hs);mpp_frame_set_ver_stride(f,p.vs);
  mpp_frame_set_fmt(f,p.format);mpp_frame_set_buffer(f,input);mpp_frame_set_pts(f,RK_S64(m.h.seq));
  auto r=p.api->encode_put_frame(p.ctx,f);mpp_frame_deinit(&f);ok(r,"put_frame");
  bool eoi=false;uint64_t deadline=now_us()+500000;
  while(!eoi){
    if(now_us()>deadline)throw std::runtime_error("MPP AU completion timeout; do not QBUF held input");
    MppPacket pkt=nullptr;r=p.api->encode_get_packet(p.ctx,&pkt);
    if(!pkt){if(r!=MPP_OK&&r!=MPP_ERR_TIMEOUT)ok(r,"get_packet");std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
    try {
      ok(r,"get_packet");
      if(mpp_packet_get_pts(pkt)!=RK_S64(m.h.seq))throw std::runtime_error("MPP reordered/mismatched PTS");
      size_t n=mpp_packet_get_length(pkt);if(n>max_au-m.bytes.size())throw std::runtime_error("MPP AU exceeds bound");
      auto* data=static_cast<uint8_t*>(mpp_packet_get_pos(pkt));if(n&&!data)throw std::runtime_error("MPP null packet payload");
      if(n)m.bytes.insert(m.bytes.end(),data,data+n);
      eoi=!mpp_packet_is_partition(pkt)||mpp_packet_is_eoi(pkt);
    }catch(...){mpp_packet_deinit(&pkt);throw;}
    mpp_packet_deinit(&pkt);
  }
  m.h.done_us=now_us();m.h.size=uint32_t(m.bytes.size());
  if(inspect_annexb(p.c.codec,m.bytes).idr)m.h.flags|=flag_idr;
  validate_au(m,force);return m;
}
} // namespace rkmoon
