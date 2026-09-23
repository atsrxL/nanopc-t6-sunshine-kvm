// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_audio_control.h"

#include <atomic>
#include <algorithm>

namespace
{
    std::atomic<int> s_VolumePercent(100);
    std::atomic<bool> s_Muted(false);
}

void RkmoonAudioControl::setVolumePercent(int percent)
{
    s_VolumePercent.store(std::max(0, std::min(100, percent)), std::memory_order_relaxed);
}

int RkmoonAudioControl::volumePercent()
{
    return s_VolumePercent.load(std::memory_order_relaxed);
}

void RkmoonAudioControl::setMuted(bool muted)
{
    s_Muted.store(muted, std::memory_order_relaxed);
}

bool RkmoonAudioControl::isMuted()
{
    return s_Muted.load(std::memory_order_relaxed);
}

float RkmoonAudioControl::gain()
{
    if (s_Muted.load(std::memory_order_relaxed)) {
        return 0.0f;
    }

    return s_VolumePercent.load(std::memory_order_relaxed) / 100.0f;
}

void RkmoonAudioControl::applyGain(void* buffer, int sampleCount, bool isFloat)
{
    float g = gain();

    // Attenuation only, so no clipping is possible and the common case costs nothing.
    if (g >= 1.0f || buffer == nullptr || sampleCount <= 0) {
        return;
    }

    if (isFloat) {
        float* samples = static_cast<float*>(buffer);
        for (int i = 0; i < sampleCount; i++) {
            samples[i] *= g;
        }
    }
    else {
        short* samples = static_cast<short*>(buffer);
        for (int i = 0; i < sampleCount; i++) {
            samples[i] = static_cast<short>(samples[i] * g);
        }
    }
}
