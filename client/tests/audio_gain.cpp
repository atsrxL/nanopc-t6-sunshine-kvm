// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_audio_control.h"
#include <cassert>

int main()
{
    short pcm[] = {32767, -32768, 1000, -1000};
    float floating[] = {1.0f, -1.0f};
    RkmoonAudioControl::setVolumePercent(50);
    RkmoonAudioControl::setMuted(false);
    RkmoonAudioControl::applyGain(pcm, 4, false);
    RkmoonAudioControl::applyGain(floating, 2, true);
    assert(pcm[0] == 16383 && pcm[1] == -16384 && pcm[2] == 500 && pcm[3] == -500);
    assert(floating[0] == 0.5f && floating[1] == -0.5f);
    RkmoonAudioControl::setMuted(true);
    RkmoonAudioControl::applyGain(pcm, 4, false);
    assert(pcm[0] == 0 && pcm[1] == 0);
    RkmoonAudioControl::setMuted(false);
    RkmoonAudioControl::setVolumePercent(105);
    assert(RkmoonAudioControl::volumePercent() == 100);
    RkmoonAudioControl::setVolumePercent(-1);
    assert(RkmoonAudioControl::volumePercent() == 0);
    return 0;
}
