/* SPDX-License-Identifier: GPL-2.0-only */
#include "frame.h"
#include <stdlib.h>

int HostCanvasUpdate(HostCanvas_t* canvas, const OpenBGI_Frame_t* frame, AndroidDamage_t* damage)
{
    int w = frame->width, h = frame->height, bpp = frame->bpp;
    int bytes = bpp / 8;
    int64_t area = (int64_t)w * h;
    *damage = (AndroidDamage_t){0};
    if(!frame->pixels || w <= 0 || h <= 0 || area > 8192LL * 4096 ||
       (bpp != 16 && bpp != 32) || frame->pitch < (int64_t)w * bytes ||
       (uint64_t)frame->pitch * (h - 1) + (uint64_t)w * bytes > SIZE_MAX)
    {
        canvas->bpp = 0;
        return 0;
    }
    if((size_t)area > canvas->capacity)
    {
        uint32_t* pixels = realloc(canvas->pixels, (size_t)area * sizeof *pixels);
        if(!pixels) {
            canvas->bpp = 0;
            return 0;
        }
        canvas->pixels = pixels;
        canvas->capacity = (size_t)area;
    }
    if(w != canvas->width || h != canvas->height || bpp != canvas->bpp ||
       frame->changes < 0 || frame->changes > OPENBGI_FRAME_CHANGES)
        *damage = (AndroidDamage_t){0, 0, w, h};
    else for(int i = 0; i < frame->changes; ++i)
    {
        OpenBGI_Rect_t rect = frame->change[i];
        if(rect.w <= 0 || rect.h <= 0) continue;
        int64_t r = (int64_t)rect.x + rect.w, b = (int64_t)rect.y + rect.h;
        AndroidDamageAdd(damage, rect.x, rect.y, r > w ? w : (int)r, b > h ? h : (int)b, w, h);
    }
    canvas->width = w;
    canvas->height = h;
    canvas->bpp = bpp;
    for(int y = damage->t; y < damage->b; ++y)
    {
        const uint8_t* src = frame->pixels + (size_t)y * frame->pitch + (size_t)damage->l * bytes;
        uint32_t* dst = canvas->pixels + (size_t)y * w + damage->l;
        if(bpp == 32)
            for(int x = damage->l; x < damage->r; ++x, src += 4)
                *dst++ = UINT32_C(0xff000000) | (uint32_t)src[0] << 16 | (uint32_t)src[1] << 8 | src[2];
        else
            for(int x = damage->l; x < damage->r; ++x, src += 2)
            {
                unsigned rgb = src[0] | (unsigned)src[1] << 8;
                unsigned r = (rgb >> 10 & 31) * 255 / 31;
                unsigned g = (rgb >> 5 & 31) * 255 / 31;
                unsigned b = (rgb & 31) * 255 / 31;
                *dst++ = UINT32_C(0xff000000) | b << 16 | g << 8 | r;
            }
    }
    return 1;
}
