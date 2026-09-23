// SPDX-License-Identifier: GPL-3.0-or-later
// Dedicated HDMI ALSA -> stereo Opus capture. Never open an implicit/default microphone.
#include "rkmoon_bridge.hpp"
#include "rkmoon_audio_pcm.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include "src/platform/common.h"
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
using audio_io::Pcm;
struct OpusDeleter { void operator()(OpusEncoder *p) const { if (p) opus_encoder_destroy(p); } };
using Encoder = std::unique_ptr<OpusEncoder, OpusDeleter>;
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
  snd_pcm_uframes_t capture_period = 0;
  auto retry_at = std::chrono::steady_clock::now();
  auto next_packet = retry_at;
  uint64_t recovered = 0, silent = 0, stale = 0, xruns = 0;
  platf::set_thread_name("rkmoon::audio");
  while (!shutdown->peek()) {
    auto now = std::chrono::steady_clock::now();
    if (!pcm && now >= retry_at) {
      pcm = audio_io::open_pcm(device, frames, capture_period);
      retry_at = now + 2s;
      if (pcm) {
        ++recovered;
        BOOST_LOG(info) << "RKMoon 48k stereo HDMI capture ready (open count " << recovered
                        << ", bitrate " << bitrate << ", period_frames " << capture_period << ")";
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
    bool cleared_stale = false;
    if (pcm) {
      // An ALSA device can expose old buffered PCM after scheduler pauses or
      // recovery. Discard the capture ring, never replay stale sound as new.
      auto available = snd_pcm_avail_update(pcm.get());
      if (available == -EPIPE) ++xruns;
      // A hardware capture period can exceed a 5ms Opus packet. Allow one
      // native period plus a packet, but never carry several periods of lag.
      if (available > std::max<snd_pcm_sframes_t>(frames * 2, capture_period + frames)) {
        ++stale;
        cleared_stale = snd_pcm_drop(pcm.get()) >= 0 && snd_pcm_prepare(pcm.get()) >= 0;
        if (!cleared_stale) pcm.reset();
        valid = false;
      } else if (available < 0 && available != -EAGAIN) {
        pcm.reset();
        valid = false;
      }
      int received = 0;
      auto read_budget_ms = std::max(config.packetDuration, int((capture_period + 47) / 48)) + 5;
      auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(read_budget_ms);
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
        if (!cleared_stale) {
          pcm.reset();
          retry_at = std::chrono::steady_clock::now() + 2s;
          BOOST_LOG(warning) << "RKMoon HDMI audio interrupted; sending silence and retrying";
        } else if (stale <= 3 || stale % 100 == 0) {
          BOOST_LOG(warning) << "RKMoon HDMI stale audio ring discarded; stale_resets=" << stale;
        }
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
