// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <alsa/asoundlib.h>
#include <algorithm>
#include <memory>

namespace rkmoon_sunshine::audio_io {
struct PcmDeleter { void operator()(snd_pcm_t *p) const { if (p) snd_pcm_close(p); } };
using Pcm = std::unique_ptr<snd_pcm_t, PcmDeleter>;

inline Pcm open_pcm(const char *device, int packet_frames, snd_pcm_uframes_t &period_out) {
  snd_pcm_t *raw = nullptr;
  int rc = snd_pcm_open(&raw, device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
  if (rc < 0) return {};
  Pcm pcm(raw);
  snd_pcm_hw_params_t *hw;
  snd_pcm_hw_params_alloca(&hw);
  unsigned int rate = 48000;
  snd_pcm_uframes_t period = packet_frames;
  int direction = 0;
  // hw: requests native format; explicit plughw: MAY convert device rate/format.
  if ((rc = snd_pcm_hw_params_any(pcm.get(), hw)) < 0 ||
      (rc = snd_pcm_hw_params_set_access(pcm.get(), hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
      (rc = snd_pcm_hw_params_set_format(pcm.get(), hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
      (rc = snd_pcm_hw_params_set_channels(pcm.get(), hw, 2)) < 0 ||
      (rc = snd_pcm_hw_params_set_rate(pcm.get(), hw, rate, 0)) < 0 ||
      (rc = snd_pcm_hw_params_set_period_size_near(pcm.get(), hw, &period, &direction)) < 0) return {};
  // Native period may exceed a 5ms packet. Never permit >100ms device ring.
  snd_pcm_uframes_t buffer = std::max<snd_pcm_uframes_t>(period * 3, packet_frames * 3);
  if (buffer > 4800 || snd_pcm_hw_params_set_buffer_size_near(pcm.get(), hw, &buffer) < 0 ||
      buffer > 4800 || snd_pcm_hw_params(pcm.get(), hw) < 0) return {};
  unsigned int actual_rate = 0, actual_channels = 0;
  snd_pcm_format_t actual_format = SND_PCM_FORMAT_UNKNOWN;
  snd_pcm_uframes_t actual_buffer = 0, actual_period = 0;
  if (snd_pcm_hw_params_get_rate(hw, &actual_rate, nullptr) < 0 ||
      snd_pcm_hw_params_get_period_size(hw, &actual_period, &direction) < 0 ||
      actual_period == 0 || actual_period > 4800 ||
      snd_pcm_hw_params_get_buffer_size(hw, &actual_buffer) < 0 || actual_buffer > 4800 ||
      snd_pcm_hw_params_get_channels(hw, &actual_channels) < 0 ||
      snd_pcm_hw_params_get_format(hw, &actual_format) < 0 ||
      actual_rate != 48000 || actual_channels != 2 || actual_format != SND_PCM_FORMAT_S16_LE) return {};
  if (snd_pcm_prepare(pcm.get()) < 0) return {};
  period_out = actual_period;
  return pcm;
}
} // namespace rkmoon_sunshine::audio_io
