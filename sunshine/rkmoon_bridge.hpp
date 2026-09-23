// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "src/video.h"
namespace rkmoon_sunshine {
// This is a dedicated patched executable, never an automatic fallback mode in an installed Sunshine.
bool enabled() noexcept;
int probe();
bool request_supported(const video::config_t&) noexcept;
void capture(safe::mail_t mail,video::config_t config,void* channel_data);
void key(uint16_t vk,bool release,uint8_t flags);
void relative(int x,int y);
void button(int number,bool release);
void scroll(int x,int y);
} // namespace rkmoon_sunshine
