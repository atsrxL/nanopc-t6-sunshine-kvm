// SPDX-License-Identifier: GPL-3.0-or-later
// Layout/import approach informed by nanopc-t6-kvm native/capture.c (GPL-3.0-or-later).
// Unlike that reference, this implementation NEVER changes source timings or format.
#include "rkmoon/capture.hpp"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
namespace rkmoon {
namespace {
int ctl(int fd,unsigned long op,void* p) {int r;do {r=ioctl(fd,op,p);}while(r<0&&errno==EINTR);return r;}
void check(int r,const char* msg) {if(r<0)throw std::runtime_error(std::string(msg)+": "+std::strerror(errno));}
double cadence(const v4l2_dv_timings& t) {
  uint64_t w=uint64_t(t.bt.width)+t.bt.hfrontporch+t.bt.hsync+t.bt.hbackporch;
  uint64_t h=uint64_t(t.bt.height)+t.bt.vfrontporch+t.bt.vsync+t.bt.vbackporch;
  return w&&h?double(t.bt.pixelclock)/double(w*h):0;
}
}
Capture::Capture(const std::string& device):fd_(open(device.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC)) {
  check(fd_.get(),"open V4L2"); struct stat st{}; check(fstat(fd_.get(),&st),"fstat V4L2");
  if(!S_ISCHR(st.st_mode)) throw std::runtime_error("V4L2 path is not a character device");
  v4l2_capability cap{};check(ctl(fd_.get(),VIDIOC_QUERYCAP,&cap),"QUERYCAP");
  auto c=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
  if(!(c&V4L2_CAP_VIDEO_CAPTURE_MPLANE)||!(c&V4L2_CAP_STREAMING)) throw std::runtime_error("requires streaming V4L2 MPLANE capture");
  query();
}
void Capture::query() {
  check(ctl(fd_.get(),VIDIOC_QUERY_DV_TIMINGS,&timings_),"QUERY_DV_TIMINGS (no signal?)");
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  check(ctl(fd_.get(),VIDIOC_G_FMT,&f),"G_FMT");format_=f.fmt.pix_mp;
  signal_fps=cadence(timings_);
  layout.width=format_.width; layout.height=format_.height;
  layout.stride=format_.plane_fmt[0].bytesperline; layout.sizeimage=format_.plane_fmt[0].sizeimage;
  if(format_.pixelformat==V4L2_PIX_FMT_NV12) layout.pixels=Pixels::nv12;
  else if(format_.pixelformat==V4L2_PIX_FMT_BGR24) layout.pixels=Pixels::bgr24;
  else throw std::runtime_error("v1 only accepts one-plane native NV12/BGR24; do not auto S_FMT");
  layout.transfer=(format_.xfer_func==V4L2_XFER_FUNC_SRGB || (format_.xfer_func==V4L2_XFER_FUNC_DEFAULT&&format_.colorspace==V4L2_COLORSPACE_SRGB))?13:1;
  layout.full_range=layout.pixels==Pixels::nv12&&format_.quantization==V4L2_QUANTIZATION_FULL_RANGE;
  layout.rgb_limited=layout.pixels==Pixels::bgr24&&format_.quantization==V4L2_QUANTIZATION_LIM_RANGE;
}
void Capture::describe() const {
  std::cerr<<"{\"kind\":\"capture_format\",\"width\":"<<layout.width<<",\"height\":"<<layout.height
    <<",\"fourcc\":"<<format_.pixelformat<<",\"planes\":"<<unsigned(format_.num_planes)<<",\"stride\":"<<layout.stride
    <<",\"sizeimage\":"<<layout.sizeimage<<",\"fps\":"<<signal_fps<<",\"colorspace\":"<<format_.colorspace
    <<",\"ycbcr_enc\":"<<unsigned(format_.ycbcr_enc)<<",\"quantization\":"<<unsigned(format_.quantization)
    <<",\"xfer_func\":"<<unsigned(format_.xfer_func)<<"}\n";
}
void Capture::validate(const Config& c) const {
  c.validate();
  if(timings_.type!=V4L2_DV_BT_656_1120||timings_.bt.interlaced||format_.field!=V4L2_FIELD_NONE||format_.num_planes!=1) throw std::runtime_error("unsupported interlaced/multiplane layout");
  if(c.width!=layout.width||c.height!=layout.height||timings_.bt.width!=c.width||timings_.bt.height!=c.height) throw std::runtime_error("client/source dimensions do not match; no scaling or modeset");
  if(!std::isfinite(signal_fps)||std::abs(signal_fps-double(c.fps_x100)/100)>0.15) throw std::runtime_error("source/client cadence mismatch");
  if(format_.colorspace!=V4L2_COLORSPACE_REC709&&format_.colorspace!=V4L2_COLORSPACE_SRGB) throw std::runtime_error("v1 requires explicitly identified BT709/sRGB SDR input");
  if(format_.xfer_func!=V4L2_XFER_FUNC_DEFAULT&&format_.xfer_func!=V4L2_XFER_FUNC_709&&format_.xfer_func!=V4L2_XFER_FUNC_SRGB) throw std::runtime_error("HDR/unknown transfer rejected");
  if(format_.quantization>V4L2_QUANTIZATION_LIM_RANGE) throw std::runtime_error("unknown quantization range");
  auto enc=format_.ycbcr_enc;
  if(enc==V4L2_YCBCR_ENC_DEFAULT) enc=V4L2_MAP_YCBCR_ENC_DEFAULT(format_.colorspace);
  if(layout.pixels==Pixels::nv12 && enc!=V4L2_YCBCR_ENC_709) throw std::runtime_error("v1 NV12 requires BT709 matrix");
  // No hidden chroma offset/padding heuristic. Only the explicitly packed layout is accepted.
  uint64_t bytes=uint64_t(layout.stride)*layout.height*(layout.pixels==Pixels::nv12?3:2)/2;
  if(layout.stride<uint64_t(layout.width)*(layout.pixels==Pixels::bgr24?3:1)||bytes!=layout.sizeimage) throw std::runtime_error("ambiguous packed plane layout/sizeimage");
}
void Capture::start(bool export_dma) {
  if(streaming_||!buffers_.empty()) throw std::runtime_error("capture already started");
  // Advisory lock protects cooperating instances, NOT older services. Operator must grant ownership.
  check(flock(fd_.get(),LOCK_EX|LOCK_NB),"capture advisory lock");
  v4l2_event_subscription sub{};sub.type=V4L2_EVENT_SOURCE_CHANGE;
  if(ctl(fd_.get(),VIDIOC_SUBSCRIBE_EVENT,&sub)<0&&errno!=EINVAL) check(-1,"SUBSCRIBE_EVENT");
  v4l2_requestbuffers req{};req.count=4;req.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;req.memory=V4L2_MEMORY_MMAP;
  check(ctl(fd_.get(),VIDIOC_REQBUFS,&req),"REQBUFS; do not change CMA/boot settings automatically");
  if(req.count<2||req.count>4) throw std::runtime_error("unexpected capture buffer count");
  buffers_.resize(req.count);
  for(uint32_t i=0;i<req.count;++i) {
    v4l2_plane plane{};v4l2_buffer b{};b.type=req.type;b.memory=req.memory;b.index=i;b.length=1;b.m.planes=&plane;
    check(ctl(fd_.get(),VIDIOC_QUERYBUF,&b),"QUERYBUF");
    auto& dst=buffers_[i];dst.size=plane.length;
    dst.data=mmap(nullptr,dst.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd_.get(),plane.m.mem_offset);
    if(dst.data==MAP_FAILED){dst.data=nullptr;check(-1,"MMAP");}
    if(dst.size<layout.sizeimage) throw std::runtime_error("capture buffer shorter than sizeimage");
    if(export_dma) {
      v4l2_exportbuffer exp{};exp.type=req.type;exp.index=i;exp.plane=0;exp.flags=O_CLOEXEC;
      if(ctl(fd_.get(),VIDIOC_EXPBUF,&exp)==0) dst.dma.reset(exp.fd);
      // An explicit allow-copy gate lives in Encoder; this does not silently change hardware encoding.
    }
    dst.dequeued=true;release(i);
  }
  auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  check(ctl(fd_.get(),VIDIOC_STREAMON,&type),"STREAMON");streaming_=true;
}
void Capture::release(uint32_t i) {
  auto& buf=buffers_.at(i);
  if(!buf.dequeued) throw std::runtime_error("double QBUF");
  v4l2_plane p{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=V4L2_MEMORY_MMAP;b.index=i;b.length=1;b.m.planes=&p;
  check(ctl(fd_.get(),VIDIOC_QBUF,&b),"QBUF");buf.dequeued=false;
}
Capture::Frame Capture::latest(std::chrono::milliseconds timeout) {
  pollfd p{fd_.get(),POLLIN|POLLPRI,0};int r;
  do{r=poll(&p,1,int(timeout.count()));}while(r<0&&errno==EINTR);
  check(r,"capture poll"); if(!r) throw std::runtime_error("HDMI frame timeout");
  if(p.revents&(POLLERR|POLLHUP|POLLNVAL|POLLPRI)) throw std::runtime_error("HDMI change/error; reconnect required");
  Frame latest{};bool have=false;
  // Bound drain to the buffer count; QBUF may allow new frames while draining.
  for(size_t attempt=0;attempt<buffers_.size();++attempt) {
    v4l2_plane plane{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=V4L2_MEMORY_MMAP;b.length=1;b.m.planes=&plane;
    if(ctl(fd_.get(),VIDIOC_DQBUF,&b)<0){if(errno==EAGAIN)break;check(-1,"DQBUF");}
    auto& buf=buffers_.at(b.index);if(buf.dequeued) throw std::runtime_error("duplicate DQBUF");buf.dequeued=true;
    if(plane.bytesused>buf.size||plane.data_offset>plane.bytesused||plane.bytesused-plane.data_offset<layout.sizeimage) throw std::runtime_error("invalid capture plane bounds");
    if(b.flags&V4L2_BUF_FLAG_ERROR){release(b.index);++raw_skipped;continue;}
    if(have){release(latest.index);++raw_skipped;}
    latest={b.index,b.sequence,b.flags,plane.data_offset,plane.bytesused,now_us(),uint64_t(b.timestamp.tv_sec)*1000000+uint64_t(b.timestamp.tv_usec)};have=true;
  }
  if(!have) throw std::runtime_error("no valid frame after capture wake");
  return latest;
}
void Capture::unchanged() {
  v4l2_dv_timings t{};check(ctl(fd_.get(),VIDIOC_QUERY_DV_TIMINGS,&t),"signal lost");
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;check(ctl(fd_.get(),VIDIOC_G_FMT,&f),"format changed");
  auto& a=format_;auto& b=f.fmt.pix_mp;
  if(t.type!=timings_.type||t.bt.interlaced||t.bt.width!=a.width||t.bt.height!=a.height||std::abs(cadence(t)-signal_fps)>0.15||
     a.width!=b.width||a.height!=b.height||a.pixelformat!=b.pixelformat||a.field!=b.field||a.num_planes!=b.num_planes||a.colorspace!=b.colorspace||a.ycbcr_enc!=b.ycbcr_enc||a.quantization!=b.quantization||a.xfer_func!=b.xfer_func||a.plane_fmt[0].bytesperline!=b.plane_fmt[0].bytesperline||a.plane_fmt[0].sizeimage!=b.plane_fmt[0].sizeimage) throw std::runtime_error("HDMI timing/format epoch changed");
}
Capture::~Capture() {
  // Caller MUST destroy Encoder before Capture. Outstanding buffers are never QBUF'd on errors.
  if(streaming_){auto type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;ctl(fd_.get(),VIDIOC_STREAMOFF,&type);}
  for(auto& b:buffers_) if(b.data) munmap(b.data,b.size);
}
} // namespace rkmoon
