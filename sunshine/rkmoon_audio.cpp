// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated HDMI ALSA -> stereo Opus capture. Never open an implicit/default microphone.
#include "rkmoon_bridge.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include <alsa/asoundlib.h>
#include <opus/opus.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace rkmoon_sunshine {
namespace {
using namespace std::chrono_literals;
struct PcmDeleter { void operator()(snd_pcm_t *p) const { if (p) snd_pcm_close(p); } };
using Pcm = std::unique_ptr<snd_pcm_t, PcmDeleter>;
struct OpusDeleter { void operator()(OpusEncoder *p) const { if (p) opus_encoder_destroy(p); } };
using Encoder = std::unique_ptr<OpusEncoder, OpusDeleter>;

Pcm open_pcm(const char *device) {
  snd_pcm_t *raw = nullptr;
  int rc = snd_pcm_open(&raw, device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
  if (rc < 0) return {};
  Pcm pcm(raw);
  snd_pcm_hw_params_t *hw;
  snd_pcm_hw_params_alloca(&hw);
  unsigned int rate = 48000;
  snd_pcm_uframes_t period = 480;
  int direction = 0;
  // An explicit hw: request is native; an explicit plughw: request MAY convert
  // device format/rate in ALSA. Both yield exactly 48k stereo to our encoder.
  if ((rc = snd_pcm_hw_params_any(pcm.get(), hw)) < 0 ||
      (rc = snd_pcm_hw_params_set_access(pcm.get(), hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
      (rc = snd_pcm_hw_params_set_format(pcm.get(), hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
      (rc = snd_pcm_hw_params_set_channels(pcm.get(), hw, 2)) < 0 ||
      (rc = snd_pcm_hw_params_set_rate(pcm.get(), hw, rate, 0)) < 0 ||
      (rc = snd_pcm_hw_params_set_period_size_near(pcm.get(), hw, &period, &direction)) < 0) return {};
  // Limit the driver-side backlog explicitly. Typical target 30ms; tolerate
  // devices with a larger native period only up to 100ms, then refuse capture.
  snd_pcm_uframes_t buffer = std::max<snd_pcm_uframes_t>(period * 3, 1440);
  if (buffer > 4800 || snd_pcm_hw_params_set_buffer_size_near(pcm.get(), hw, &buffer) < 0 ||
      buffer > 4800 || snd_pcm_hw_params(pcm.get(), hw) < 0) return {};
  unsigned int actual_rate = 0, actual_channels = 0;
  snd_pcm_format_t actual_format = SND_PCM_FORMAT_UNKNOWN;
  snd_pcm_uframes_t actual_buffer = 0;
  if (snd_pcm_hw_params_get_rate(hw, &actual_rate, nullptr) < 0 ||
      snd_pcm_hw_params_get_buffer_size(hw, &actual_buffer) < 0 || actual_buffer > 4800 ||
      snd_pcm_hw_params_get_channels(hw, &actual_channels) < 0 ||
      snd_pcm_hw_params_get_format(hw, &actual_format) < 0 ||
      actual_rate != 48000 || actual_channels != 2 || actual_format != SND_PCM_FORMAT_S16_LE) return {};
  if (snd_pcm_prepare(pcm.get()) < 0) return {};
  return pcm;
}
} // namespace

void audio_capture(safe::mail_t mail, audio::config_t config, void *channel_data) {
  auto shutdown = mail->event<bool>(mail::shutdown);
  const char *device = std::getenv("RKMOON_AUDIO_DEVICE");
  if (!config::audio.stream || !device || !*device) {
    BOOST_LOG(info) << "RKMoon HDMI audio disabled: explicit RKMOON_AUDIO_DEVICE required";
    shutdown->view();
    return;
  }
  // One supported Opus layout; don't encode a stereo buffer as surround data.
  if (config.channels != 2 || config.flags[audio::config_t::CUSTOM_SURROUND_PARAMS] ||
      (config.packetDuration != 5 && config.packetDuration != 10 && config.packetDuration != 20)) {
    BOOST_LOG(warning) << "RKMoon audio requires stereo and 5/10/20ms packets; video remains available";
    shutdown->view();
    return;
  }
  int error = 0;
  Encoder encoder(opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &error));
  const int bitrate = config.flags[audio::config_t::HIGH_QUALITY] ? 512000 : 96000;
  if (!encoder || error != OPUS_OK ||
      opus_encoder_ctl(encoder.get(), OPUS_SET_BITRATE(bitrate)) != OPUS_OK ||
      opus_encoder_ctl(encoder.get(), OPUS_SET_VBR(0)) != OPUS_OK) {
    BOOST_LOG(error) << "RKMoon Opus encoder initialization failed";
    shutdown->view();
    return;
  }
  auto packets = mail::man->queue<audio::packet_t>(mail::audio_packets);
  const int frames = config.packetDuration * 48;
  std::vector<int16_t> pcm_samples(size_t(frames) * 2);
  std::vector<opus_int16> encoded_samples(pcm_samples.size());
  Pcm pcm;
  auto retry_at = std::chrono::steady_clock::now();
  auto next_packet = retry_at;
  uint64_t recovered = 0, silent = 0, stale = 0, xruns = 0;
  platf::set_thread_name("rkmoon::audio");
  while (!shutdown->peek()) {
    auto now = std::chrono::steady_clock::now();
    if (!pcm && now >= retry_at) {
      pcm = open_pcm(device);
      retry_at = now + 2s;
      if (pcm) {
        ++recovered;
        BOOST_LOG(info) << "RKMoon 48k stereo HDMI capture ready (open count " << recovered << ", bitrate " << bitrate << ")";
      } else if (recovered == 0) {
        BOOST_LOG(warning) << "RKMoon HDMI audio unavailable; sending initial silence; video/input continue";
      }
    }
    // Real-time packet clock from the first audio interval, including no source.
    if (now < next_packet) {
      shutdown->view(std::min(20ms, std::chrono::duration_cast<std::chrono::milliseconds>(next_packet - now)));
      continue;
    }
    std::fill(pcm_samples.begin(), pcm_samples.end(), 0);
    bool valid = bool(pcm);
    if (pcm) {
      // An ALSA device can expose old buffered PCM after scheduler pauses or
      // recovery. Discard the capture ring, never replay stale sound as new.
      auto available = snd_pcm_avail_update(pcm.get());
      if (available == -EPIPE) ++xruns;
      if (available > frames * 2) {
        ++stale;
        if (snd_pcm_drop(pcm.get()) < 0 || snd_pcm_prepare(pcm.get()) < 0) pcm.reset();
        valid = false;
      } else if (available < 0 && available != -EAGAIN) {
        pcm.reset();
        valid = false;
      }
      int received = 0;
      auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.packetDuration + 5);
      while (valid && received < frames && !shutdown->peek() && std::chrono::steady_clock::now() < deadline) {
        auto n = snd_pcm_readi(pcm.get(), pcm_samples.data() + size_t(received) * 2, frames - received);
        if (n > 0) { received += int(n); continue; }
        if (n == -EAGAIN || n == 0) { shutdown->view(1ms); continue; }
        if (n == -EPIPE) ++xruns;
        valid = false;
        break;
      }
      if (received != frames) valid = false;
      if (!valid) {
        pcm.reset();
        retry_at = std::chrono::steady_clock::now() + 2s;
        BOOST_LOG(warning) << "RKMoon HDMI audio interrupted/stale; sending silence and retrying";
        std::fill(pcm_samples.begin(), pcm_samples.end(), 0);
      }
    }
    if (!valid) ++silent;
    if (shutdown->peek()) break;
    // Opus RTP timestamps and sequence numbers remain owned by the pinned Sunshine
    // stream sender; one packet represents exactly packetDuration ms (including silence).
    std::copy(pcm_samples.begin(), pcm_samples.end(), encoded_samples.begin());
    audio::buffer_t packet {1400};
    int bytes = opus_encode(encoder.get(), encoded_samples.data(), frames, std::begin(packet), (opus_int32) packet.size());
    if (bytes < 0) {
      BOOST_LOG(error) << "RKMoon Opus encode failed: " << opus_strerror(bytes);
      break;
    }
    packet.fake_resize(bytes);
    packets->raise(channel_data, std::move(packet));
    next_packet += std::chrono::milliseconds(config.packetDuration);
    now = std::chrono::steady_clock::now();
    if (next_packet < now) next_packet = now; // Never burst to catch up after a late read.
    if (next_packet > now) shutdown->view(std::min(20ms, std::chrono::duration_cast<std::chrono::milliseconds>(next_packet - now)));
  }
  BOOST_LOG(info) << "RKMoon HDMI audio capture ended; opens=" << recovered << " silent_packets=" << silent
                  << " stale_resets=" << stale << " xruns=" << xruns;
}
} // namespace rkmoon_sunshine
