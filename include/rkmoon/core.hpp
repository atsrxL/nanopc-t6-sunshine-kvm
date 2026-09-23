// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace rkmoon {
constexpr uint32_t max_au = 8U * 1024U * 1024U;
constexpr size_t header_size = 64;
enum class Codec : uint16_t { h264=1, hevc=2 };
enum class Kind : uint16_t { frame=1, ack=2, stop=3, idr=4, bitrate=5, ready=6, caps=7, error=8 };
enum : uint32_t { flag_idr=1, flag_dmabuf=2, flag_full_range=4, flag_bt709=8 };
struct Header {
  Kind kind{Kind::frame}; uint32_t size{}, flags{}; uint64_t seq{}, dequeue_us{}, submit_us{}, done_us{};
  uint32_t width{}, height{}; Codec codec{Codec::h264}; uint32_t extra{};
};
struct Message { Header h; std::vector<uint8_t> bytes; };
struct Config {
  uint32_t width=1920, height=1080, fps_x100=6000, bitrate=20000000, gop=60;
  Codec codec=Codec::hevc;
  void validate() const;
};
uint64_t now_us();
std::array<uint8_t,header_size> pack(const Header&);
Header unpack(const uint8_t*, size_t);
// One absolute monotonic deadline covers both header and payload; never extends per partial read.
Message receive(int fd, std::chrono::milliseconds timeout);
void send(int fd, const Message&, std::chrono::milliseconds timeout);
bool readable(int fd, std::chrono::milliseconds timeout);
struct Nals { bool vcl{}, idr{}, vps{}, sps{}, pps{}; size_t count{}, pictures{}; };
Nals inspect_annexb(Codec codec, const std::vector<uint8_t>& bytes);
void validate_au(const Message&, bool require_idr);
class SequenceGate {
  uint64_t last_=0; bool need_idr_=true;
public:
  void accept(const Message& m);
  void recover() { need_idr_=true; }
};
enum class Pixels { nv12, bgr24 };
struct Layout { uint32_t width{}, height{}, stride{}, sizeimage{}; Pixels pixels=Pixels::nv12; bool full_range=false; uint32_t transfer=1; };
bool direct_layout(const Layout&);
// CPU PIXEL copy/conversion only. No software video encoder exists in this project.
void to_nv12(const Layout&, const uint8_t* source, size_t source_size,
             uint8_t* dest, size_t dest_size, uint32_t stride, uint32_t vstride);
class Fd {
  int fd_=-1;
public:
  explicit Fd(int fd=-1):fd_(fd){} ~Fd();
  Fd(const Fd&)=delete; Fd& operator=(const Fd&)=delete;
  Fd(Fd&&) noexcept; Fd& operator=(Fd&&) noexcept;
  int get() const { return fd_; } int release(); void reset(int fd=-1);
};
class Child {
  int pid_=-1; Fd socket_;
public:
  Child(const std::string& executable, const std::vector<std::string>& arguments);
  ~Child(); Child(const Child&)=delete; Child& operator=(const Child&)=delete;
  int fd() const { return socket_.get(); }
  void terminate();
};
} // namespace rkmoon
