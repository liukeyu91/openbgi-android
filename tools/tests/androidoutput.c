/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../src/os/android/audio_output.h"
#include "../../src/os/android/viewport.h"
#include "../android/tests/stubs/aaudio/mock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void Viewports(void){
 assert(!AndroidViewportNearest(AndroidViewport(1920,1080,1280,720,0),1280,720));
 assert(AndroidViewportNearest(AndroidViewport(2560,1440,1280,720,0),1280,720));
 assert(AndroidViewportNearest(AndroidViewport(1920,1080,1280,720,2),1280,720));
 assert(!AndroidViewportNearest(AndroidViewport(1920,1440,1280,720,1),1280,720));
 assert(!AndroidViewportNearest(AndroidViewport(640,360,1280,720,2),1280,720));
 assert(!AndroidViewportNearest(AndroidViewport(0,0,1280,720,0),1280,720));
}
static void StreamContinuity(int source,int target){
 AndroidAudioOutput_t out={0};int frames=source/5,i,offset;int16_t* input=malloc((size_t)frames*4);int16_t* expected;
 MockAudioReset();mockAudio.nativeRate=target;
 for(i=0;i<frames;i++){input[2*i]=(int16_t)(12000*sin(6.283185307179586*1000*i/source));input[2*i+1]=(int16_t)-input[2*i];}
 assert(AndroidAudio_Open(&out,source,frames,1));assert(AndroidAudio_Write(&out,input,frames));
 assert(mockAudio.capturedFrames==target/5);expected=malloc((size_t)mockAudio.capturedFrames*4);memcpy(expected,mockAudio.captured,(size_t)mockAudio.capturedFrames*4);AndroidAudio_Close(&out);
 mockAudio.capturedFrames=0;assert(AndroidAudio_Open(&out,source,frames,1));
 for(offset=0;offset<frames;){int n=(offset%997)+1;if(n>frames-offset)n=frames-offset;assert(AndroidAudio_Write(&out,input+2*offset,n));offset+=n;}
 assert(mockAudio.capturedFrames==target/5 && memcmp(expected,mockAudio.captured,(size_t)mockAudio.capturedFrames*4)==0);
 {int crossings=0;for(i=1;i<mockAudio.capturedFrames;i++)if(mockAudio.captured[2*(i-1)]<0 && mockAudio.captured[2*i]>=0)++crossings;assert(crossings>=198 && crossings<=200);}
 if(source==target)assert(memcmp(input,mockAudio.captured,(size_t)frames*4)==0);
 AndroidAudio_Close(&out);free(input);free(expected);
}
static void Negotiation(void){
 AndroidAudioOutput_t out={0};int16_t input[882*2];int i;
 MockAudioReset();mockAudio.rejectRate=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(mockAudio.opens==2 && out.rate==48000 && mockAudio.logs>0);AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.rejectI16=1;mockAudio.format=AAUDIO_FORMAT_PCM_FLOAT;mockAudio.channels=1;
 assert(AndroidAudio_Open(&out,44100,882,1));assert(mockAudio.opens==3);
 for(i=0;i<882;i++){input[2*i]=30000;input[2*i+1]=-10000;}
 mockAudio.partial=17;assert(AndroidAudio_Write(&out,input,882));assert(mockAudio.capturedFrames==960);
 for(i=0;i<mockAudio.capturedFrames*2;i++)assert(mockAudio.captured[i]==10000);
 mockAudio.writeError=-899;assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.channels=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(AndroidAudio_Write(&out,input,882));
 for(i=0;i<mockAudio.capturedFrames*2;i++)assert(mockAudio.captured[i]==10000);
 mockAudio.zeroWrite=1;assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.format=AAUDIO_FORMAT_PCM_FLOAT;assert(AndroidAudio_Open(&out,44100,882,1));assert(AndroidAudio_Write(&out,input,882));
 for(i=0;i<mockAudio.capturedFrames;i++){assert(mockAudio.captured[2*i]==30000);assert(mockAudio.captured[2*i+1]==-10000);}AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.failStart=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(mockAudio.opens==2);AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.format=99;assert(!AndroidAudio_Open(&out,44100,882,1));assert(mockAudio.opens==5 && mockAudio.logs>0);AndroidAudio_Close(&out);
 MockAudioReset();mockAudio.nativeRate=0;assert(!AndroidAudio_Open(&out,44100,882,1));assert(mockAudio.opens==5);AndroidAudio_Close(&out);
 MockAudioReset();assert(!AndroidAudio_Open(&out,0,882,1));assert(!AndroidAudio_Open(&out,44100,0,1));
 MockAudioReset();assert(AndroidAudio_Open(&out,44100,1,1));assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 MockAudioReset();
}
int main(void){Viewports();StreamContinuity(44100,48000);StreamContinuity(48000,44100);StreamContinuity(44100,44100);StreamContinuity(8000,384000);Negotiation();puts("Android output: scaling policy, negotiation, continuous resampling, duration/pitch, mono/float, partial writes and recovery passed");return 0;}
