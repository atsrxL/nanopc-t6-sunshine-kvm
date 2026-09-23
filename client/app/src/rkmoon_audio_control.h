// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Client-side volume and mute for the KVM client. The host keeps sending audio in both
// cases: muting is a zero gain applied after Opus decoding so the decoder state and the
// renderer's buffer timing stay continuous and un-muting does not need a resync.
namespace RkmoonAudioControl
{
    // 0 (silent) to 100 (unattenuated). Values are clamped.
    void setVolumePercent(int percent);
    int volumePercent();

    void setMuted(bool muted);
    bool isMuted();

    // Effective linear gain, including mute. 1.0 means "leave the samples alone".
    float gain();

    // Applies the gain in place. sampleCount is the total number of samples across all
    // channels (frames * channelCount). Called from the audio decoder thread.
    void applyGain(void* buffer, int sampleCount, bool isFloat);
}
