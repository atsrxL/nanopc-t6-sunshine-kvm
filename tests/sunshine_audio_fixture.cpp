// SPDX-License-Identifier: GPL-3.0-or-later
// Links actual rkmoon_audio.cpp from the built server. ALSA null + injected
// availability/xrun statuses exercise production control flow, not HDMI hardware.
#include "src/rkmoon/rkmoon_bridge.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/logging.h"
#include <alsa/asoundlib.h>
#include <opus/opus.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

std::atomic_int availability_calls {0};
std::atomic_int injected_stale {0};
std::atomic_int injected_xrun {0};
extern "C" snd_pcm_sframes_t __wrap_snd_pcm_avail_update(snd_pcm_t *) {
  const int n=++availability_calls;
  if (n==6) { ++injected_stale; return 48000; }
  if (n==12) { ++injected_xrun; return -EPIPE; }
  // ALSA null has no physical device clock; expose an empty ring, then readi
  // returns synthetic samples immediately. This is explicitly an injected model.
  return 0;
}
int main(int argc,char **argv) {
  if (argc!=2) return 1;
  using namespace std::chrono;
  auto log=logging::init(2,argv[1]);
  config::audio.stream=true;
  for (int duration : {5,10,20}) {
    for (bool initially_absent : {false,true}) {
      setenv("RKMOON_AUDIO_DEVICE",initially_absent ? "hw:CARD=NO_SUCH_HDMI" : "null",1);
      availability_calls=0;injected_stale=0;injected_xrun=0;
      mail::man=std::make_shared<safe::mail_raw_t>();
      auto shutdown=mail::man->event<bool>(mail::shutdown);
      auto packets=mail::man->queue<audio::packet_t>(mail::audio_packets);
      audio::config_t cfg{};cfg.channels=2;cfg.packetDuration=duration;
      cfg.flags[audio::config_t::HIGH_QUALITY]=initially_absent;
      int opus_status=0;
      OpusDecoder *decoder=opus_decoder_create(48000,2,&opus_status);
      if (!decoder || opus_status!=OPUS_OK) return 2;
      std::thread capture([&]{rkmoon_sunshine::audio_capture(mail::man,cfg,nullptr);});
      auto start=steady_clock::now();
      auto end=start+milliseconds(initially_absent ? 450 : 2900);
      int count=0;bool invalid=false;
      std::vector<opus_int16> pcm(size_t(duration*48)*2);
      while (steady_clock::now()<end) {
        auto packet=packets->pop(milliseconds(100));
        if (!packet) continue;
        auto &bytes=packet->second;
        int n=opus_decode(decoder,std::begin(bytes),bytes.size(),pcm.data(),duration*48,0);
        if (n!=duration*48) {invalid=true;break;}
        ++count;
      }
      auto stop_at=steady_clock::now();
      shutdown->raise(true);
      capture.join();
      auto stop_ms=duration_cast<milliseconds>(steady_clock::now()-stop_at).count();
      opus_decoder_destroy(decoder);
      // Broad bounds permit QEMU jitter but catch a stalled source, packet
      // burst, non-Opus data, absent initial silence, or blocking shutdown.
      int expected=(initially_absent?450:2900)/duration;
      bool passed=!invalid && count>expected/3 && count<expected*2 && stop_ms<1000;
      if (!initially_absent) passed=passed && injected_stale==1 && injected_xrun==1 && availability_calls>12;
      std::cout << "production_audio_loop synthetic=" << (initially_absent?"absent-source":"alsa-null-injected")
                << " packet_ms=" << duration << " high_quality=" << initially_absent << " decoded_packets=" << count
                << " injected_stale=" << injected_stale << " injected_xrun=" << injected_xrun
                << " capture_calls=" << availability_calls << " shutdown_ms=" << stop_ms
                << " result=" << (passed?"pass":"fail") << '\n';
      if (!passed) return 3;
    }
  }
}
