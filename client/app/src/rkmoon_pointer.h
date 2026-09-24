// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "streaming/streamutils.h"
#include <algorithm>
#include <cmath>
namespace RkmoonPointer {
// Inputs and viewport must share coordinate units (SDL window units for SDL events).
// The same aspect-fit rounding as the renderer defines the visible video rectangle.
inline SDL_Rect videoRect(int width, int height, int viewportWidth, int viewportHeight) {
    if (width <= 0 || height <= 0 || viewportWidth <= 0 || viewportHeight <= 0) return {};
    SDL_Rect src{0, 0, width, height}, dst{0, 0, viewportWidth, viewportHeight};
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);
    return dst;
}
inline bool inside(double x, double y, const SDL_Rect& r) {
    return r.w > 0 && r.h > 0 && x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h;
}
struct Position { short x, y; };
// Moonlight sends referenceWidth-1 on wire. Clamp to the last video pixel,
// including drag motion outside the image; never send width/height themselves.
inline Position position(double x, double y, const SDL_Rect& r) {
    return {short(std::clamp(int(std::floor(x-r.x)), 0, std::max(0,r.w-1))),
            short(std::clamp(int(std::floor(y-r.y)), 0, std::max(0,r.h-1)))};
}
}
