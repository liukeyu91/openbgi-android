/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_HOST_FRAME_H
#define BGI_ANDROID_HOST_FRAME_H

#include "openbgi.h"
#include "../../os/android/renderer.h"

typedef struct HostCanvas {
    uint32_t* pixels;
    size_t capacity;
    int width, height, bpp;
} HostCanvas_t;

/* Copies only the part the renderer will upload. A zero bpp invalidates the copy. */
int HostCanvasUpdate(HostCanvas_t* canvas, const OpenBGI_Frame_t* frame, AndroidDamage_t* damage);

#endif
