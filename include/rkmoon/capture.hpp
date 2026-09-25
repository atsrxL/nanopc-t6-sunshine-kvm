// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.hpp"
#include <linux/videodev2.h>
namespace rkmoon {
// The HDMI receiver reports no locked source. Distinct from a changed or unsupported mode.
struct NoSignal : std::runtime_error { using std::runtime_error::runtime_error; };
class Capture {
public:
  struct Buffer { void* data=nullptr; size_t size=0; Fd dma; bool dequeued=false; };
  struct Frame { uint32_t index{}, sequence{}, flags{}, offset{}, bytes{}; uint64_t dequeue_us{}, driver_us{}; };
private:
  Fd fd_; std::vector<Buffer> buffers_; bool streaming_=false;
  v4l2_pix_format_mplane format_{}; v4l2_dv_timings timings_{};
  void query();
public:
  Layout layout; double signal_fps=0; uint64_t raw_skipped=0;
  explicit Capture(const std::string& device);
  ~Capture(); Capture(const Capture&)=delete; Capture& operator=(const Capture&)=delete;
  void describe() const;
  void validate(const Config&) const;
  void start(bool export_dma);
  void unchanged();
  Frame latest(std::chrono::milliseconds timeout);
  // Explicit release only; on exception leave DQBUF owned until encoder teardown + STREAMOFF.
  void release(uint32_t index);
  Buffer& buffer(uint32_t index) { return buffers_.at(index); }
  const std::vector<Buffer>& buffers() const { return buffers_; }
  int fd() const { return fd_.get(); }
};
} // namespace rkmoon
