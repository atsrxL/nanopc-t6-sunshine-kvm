// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cmath>
namespace rkmoon {
// Input is the HDMI viewport coordinate after Sunshine's client_to_touchport.
inline int absolute_coordinate(float value,int extent){
  if(extent<=0||!std::isfinite(value))throw std::invalid_argument("invalid absolute viewport");
  return int(std::lround(std::clamp(double(value)/extent,0.0,1.0)*65535.0))-32768;
}
struct HidEvent { enum Type { key,button,move,wheel,absolute } type;int a=0,b=0;bool down=false; };
class HidClient {
  Fd fd_;std::mutex mutex_;std::condition_variable cv_;std::deque<HidEvent> events_;
  const bool absolute_mouse_; // Immutable for the lease; selection precedes all events.
  std::atomic<bool> healthy_{true},stop_{false};std::thread thread_;size_t highwater_=0;
  void run();
public:
  explicit HidClient(const std::string& path,bool absolute_mouse=false);~HidClient();
  HidClient(const HidClient&)=delete;HidClient& operator=(const HidClient&)=delete;
  bool submit(HidEvent);bool healthy()const{return healthy_.load();}
  size_t highwater();
};
} // namespace rkmoon
