#pragma once

#include <math.h>

#include "dkc1_video.h"

/* Widescreen source width for a landscape screen of view_w x view_h pixels.
 * Same maths as the iOS host (runner/ios/ios_viewport.c): the picture stays
 * 224 source pixels tall, SNES pixels are 7:6, and the width is the smallest
 * even value that fills the screen, kept within the PPU's 256..448 capacity.
 * Screens narrower than 4:3 get the native 256 and are letterboxed by the
 * caller. At most one source column per side is clipped by the rounding. */
static inline int Dkc1AndroidRenderWidth(int view_w, int view_h) {
    if (view_w <= 0 || view_h <= 0) return kDkc1VideoNativeWidth;
    const double pixel_aspect = 7.0 / 6.0;
    const double needed =
        (double)view_w / (double)view_h * kDkc1VideoHeight / pixel_aspect;
    double even = ceil(needed / 2.0) * 2.0;
    if (even < kDkc1VideoNativeWidth) even = kDkc1VideoNativeWidth;
    if (even > kDkc1VideoMaxWidth) even = kDkc1VideoMaxWidth;
    return (int)even;
}
