// SPDX-License-Identifier: GPL-3.0-or-later
#include "rkmoon_audio_keys.h"
#include "rkmoon_audio_control.h"

int RkmoonAudioKeys::filter(const SDL_Event& event)
{
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) return 1;
    const auto key = event.key.keysym;
    int index = key.sym == SDLK_F9 ? 0 : key.sym == SDLK_F10 ? 1 : key.sym == SDLK_F11 ? 2 : -1;
    if (index < 0) return 1;
    if (event.type == SDL_KEYUP) return held[index].exchange(false) ? 0 : 1;
    if (held[index].load()) return 0; // Consume repeats even if modifiers were released.
    if (!(key.mod & KMOD_CTRL) || !(key.mod & KMOD_ALT) || !(key.mod & KMOD_SHIFT)) return 1;
    if (event.key.repeat || held[index].exchange(true)) return 0;
    if (index == 0) RkmoonAudioControl::setMuted(!RkmoonAudioControl::isMuted());
    if (index == 1) RkmoonAudioControl::setVolumePercent(RkmoonAudioControl::volumePercent() - 10);
    if (index == 2) RkmoonAudioControl::setVolumePercent(RkmoonAudioControl::volumePercent() + 10);
    return 0;
}

void RkmoonAudioKeys::reset()
{
    for (auto& value : held) value.store(false);
}
