/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_RENDERER_H
#define BGI_ANDROID_RENDERER_H
#include <stdint.h>
#include <android/native_window.h>

/* Half-open canvas coordinates. Empty rectangles never cause an upload. */
typedef struct AndroidDamage { int l,t,r,b; } AndroidDamage_t;
static inline void AndroidDamageAdd(AndroidDamage_t* d,int l,int t,int r,int b,int w,int h)
{
    if(l<0)l=0;if(t<0)t=0;if(r>w)r=w;if(b>h)b=h;
    if(l>=r || t>=b)return;
    if(d->l>=d->r || d->t>=d->b){*d=(AndroidDamage_t){l,t,r,b};return;}
    if(l<d->l)d->l=l;if(t<d->t)d->t=t;if(r>d->r)d->r=r;if(b>d->b)d->b=b;
}
typedef struct AndroidRenderStats { uint64_t frames,full,partial,bytes,errors; } AndroidRenderStats_t;
AndroidRenderStats_t OsAndroid_RenderStats(void);
/* All calls, including cleanup, run on the engine thread without the host lock. */
int OsAndroid_Render(ANativeWindow* window,unsigned generation,const uint32_t* pixels,
    int w,int h,AndroidDamage_t damage,int mode);
void OsAndroid_RenderClose(void);
#endif
