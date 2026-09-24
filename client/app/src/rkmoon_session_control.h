// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <atomic>
#include "rkmoon_diagnostics.h"
namespace RkmoonSessionControl {
inline std::atomic<bool> userQuit{false};
inline std::atomic<bool> internalQuitPending{false};
inline void initialize() {
    internalQuitPending.store(false);
    userQuit.store(false);
}
inline void stop() {
    // No SDL event allocation: Moonlight uses SDL_USEREVENT directly without
    // reserving it. SDL_RegisterEvents can return that same value. The session
    // loop checks this flag at most every 20 ms, even when its queue is full.
    internalQuitPending.store(true);
}
inline void observe(const SDL_Event& event) {
    if (event.type == SDL_QUIT) {
        userQuit.store(true);
        RkmoonDiagnostics::record("sdl-quit-observed");
    }
    else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)
        RkmoonDiagnostics::record("sdl-window-close-observed");
}
}
