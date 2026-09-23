// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <atomic>

// SDL event filters can run on the posting thread. State is atomic, never logs
// key contents, and consumes a reserved key's release even after modifiers lift.
class RkmoonAudioKeys
{
public:
    int filter(const SDL_Event& event);
    void reset();
private:
    std::atomic<bool> held[3] = {};
};
