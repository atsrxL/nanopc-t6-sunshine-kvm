// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
namespace rkmoon {
struct HidEvent { enum Type { key,button,move,wheel } type;int a=0,b=0;bool down=false; };
class HidClient {
  Fd fd_;std::mutex mutex_;std::condition_variable cv_;std::deque<HidEvent> events_;
  std::atomic<bool> healthy_{true},stop_{false};std::thread thread_;size_t highwater_=0;
  void run();
public:
  explicit HidClient(const std::string& path);~HidClient();
  HidClient(const HidClient&)=delete;HidClient& operator=(const HidClient&)=delete;
  bool submit(HidEvent);bool healthy()const{return healthy_.load();}
  size_t highwater();
};
} // namespace rkmoon
