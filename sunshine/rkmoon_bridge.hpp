// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/video.h"
#include "src/audio.h"
#include <optional>
#include <cstdlib>
#include <string_view>
namespace rkmoon_sunshine {
template<class Args> std::optional<bool> mouse_mode(const Args& args) {
  if(args.count("rkmoonMouseMode")>1)return std::nullopt;
  auto it=args.find("rkmoonMouseMode");
  if(it==args.end()||it->second=="relative")return false;
  const char* allowed=std::getenv("RKMOON_ALLOW_ABSOLUTE_MOUSE");
  if(it->second=="absolute"&&allowed&&std::string_view(allowed)=="1")return true;
  return std::nullopt;
}
// This is a dedicated patched executable, never an automatic fallback mode in an installed Sunshine.
bool enabled() noexcept;
int probe();
bool request_supported(const video::config_t&) noexcept;
void capture(safe::mail_t mail,video::config_t config,void* channel_data);
void audio_capture(safe::mail_t mail,audio::config_t config,void* channel_data);
void key(uint16_t vk,bool release,uint8_t flags);
void relative(int x,int y);
void absolute(const platf::touch_port_t& port,float x,float y);
void button(int number,bool release);
void scroll(int x,int y);
} // namespace rkmoon_sunshine
