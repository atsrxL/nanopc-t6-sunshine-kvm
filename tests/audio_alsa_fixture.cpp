// SPDX-License-Identifier: GPL-3.0-or-later
// ALSA null capture + real libopus smoke; synthetic source, NOT HDMI/audio-sync acceptance.
#include "../sunshine/rkmoon_audio_pcm.hpp"
#include <opus/opus.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  using namespace std::chrono;
  for (int duration : {5,10,20}) {
    const int frames = duration * 48;
    snd_pcm_uframes_t period = 0;
    // Simulate initial missing source, then an explicitly selected ALSA
    // null plugin. Device is synthetic; hardware timing remains untested.
    auto missing = rkmoon_sunshine::audio_io::open_pcm("hw:CARD=NO_SUCH_HDMI", frames, period);
    if (missing) return 1;
    auto pcm = rkmoon_sunshine::audio_io::open_pcm("null", frames, period);
    if (!pcm) { std::cerr << "null ALSA capture unsupported\n"; return 2; }
    int err = 0;
    OpusEncoder *encoder=opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (!encoder || err != OPUS_OK) return 3;
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(96000));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(0));
    std::vector<int16_t> samples(size_t(frames)*2, 0);
    std::array<unsigned char,1400> bytes {};
    auto next = steady_clock::now();
    int encoded = 0, read_frames = 0, silence = 0;
    for (int i=0;i<25;++i) {
      std::this_thread::sleep_until(next);
      next += milliseconds(duration);
      int received=0;
      auto deadline=steady_clock::now()+milliseconds(duration+5);
      while (received<frames && steady_clock::now()<deadline) {
        auto n=snd_pcm_readi(pcm.get(), samples.data()+size_t(received)*2, frames-received);
        if (n>0) { received+=int(n); continue; }
        if (n==-EAGAIN || n==0) { std::this_thread::sleep_for(1ms);continue; }
        break;
      }
      read_frames+=received;
      if (received!=frames) { ++silence; std::fill(samples.begin(),samples.end(),0); }
      int n=opus_encode(encoder,samples.data(),frames,bytes.data(),bytes.size());
      if (n<=0) return 4;
      ++encoded;
    }
    pcm.reset();
    pcm=rkmoon_sunshine::audio_io::open_pcm("null",frames,period);
    if (!pcm || encoded!=25 || period==0) return 5;
    opus_encoder_destroy(encoder);
    std::cout << "synthetic_alsa_null packet_ms=" << duration << " samples=" << frames
              << " packets=" << encoded << " frames_read=" << read_frames
              << " silent_packets=" << silence << " reopen=ok\n";
  }
}
