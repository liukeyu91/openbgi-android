/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_VIEWPORT_H
#define BGI_ANDROID_VIEWPORT_H
#include <stdint.h>
typedef struct Viewport { int x,y,w,h; } Viewport_t;
static inline Viewport_t AndroidViewport(int sw,int sh,int cw,int ch,int mode)
{
    int w=sw>0?sw:0,h=sh>0?sh:0,k;
    Viewport_t v={0,0,0,0};
    if(cw<=0 || ch<=0)return v;
    k=w/cw<h/ch?w/cw:h/ch;
    if(mode==2 && k>=1){w=cw*k;h=ch*k;}
    else if(mode!=1){if((int64_t)w*ch>(int64_t)h*cw)w=(int)((int64_t)h*cw/ch);else h=(int)((int64_t)w*ch/cw);}
    v.x=(sw-w)/2;v.y=(sh-h)/2;v.w=w;v.h=h;return v;
}
#endif
