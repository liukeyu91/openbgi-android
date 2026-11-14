/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../src/os/android/viewport.h"
#include <stdio.h>
int main(void){
    int widths[]={0,1,640,1280,1920,2248},heights[]={0,1,480,720,1080},canvas[][2]={{1280,720},{1920,1080},{800,600}};
    unsigned a,b,c;int mode;
    for(a=0;a<sizeof widths/sizeof widths[0];a++)for(b=0;b<sizeof heights/sizeof heights[0];b++)for(c=0;c<3;c++)for(mode=0;mode<3;mode++){
        Viewport_t v=AndroidViewport(widths[a],heights[b],canvas[c][0],canvas[c][1],mode);printf("%d,%d,%d,%d\n",v.x,v.y,v.w,v.h);
    }
    return 0;
}
