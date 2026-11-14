/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_CANVAS_H
#define BGI_ANDROID_CANVAS_H
#include <stdint.h>
#include <string.h>

/* Convert an already clipped row into the logical RGBA canvas. memcpy
 * permits unaligned source views; the destination is an aligned canvas.
 * Kept free of Android/JNI dependencies for byte-for-byte host checks. */
static inline void AndroidCanvasCopyRow(uint32_t* dst,const uint8_t* src,int count,int bpp)
{
    int x;
    if(bpp==32) {
        for(x=0;x<count;x++) {
            uint32_t p;memcpy(&p,src+(size_t)x*4,4);
            dst[x]=0xff000000u|(p&0xff00u)|((p>>16)&255)|((p&255)<<16);
        }
    } else {
        for(x=0;x<count;x++) {
            uint16_t p;unsigned r,g,b;memcpy(&p,src+(size_t)x*2,2);
            r=(p>>10)&31;g=(p>>5)&31;b=p&31;
            r=(r<<3)|(r>>2);g=(g<<3)|(g>>2);b=(b<<3)|(b>>2);
            dst[x]=r|(g<<8)|(b<<16)|0xff000000u;
        }
    }
}
#endif
