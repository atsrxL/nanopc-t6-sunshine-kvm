// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "capture.hpp"
#include <memory>
namespace rkmoon {
class Encoder {
  struct Impl; std::unique_ptr<Impl> p_;
public:
  Encoder(); ~Encoder(); Encoder(const Encoder&)=delete; Encoder& operator=(const Encoder&)=delete;
  void open(const Config&, const Layout&, const std::vector<Capture::Buffer>* buffers, bool allow_copy);
  Message encode(const Capture::Frame&, Capture::Buffer* buffer, bool idr);
  void bitrate(uint32_t bps);
  bool direct() const;
};
} // namespace rkmoon
