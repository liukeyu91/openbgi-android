/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../src/os/android/audio_output.h"
#include "../../src/os/android/viewport.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct AAudioStreamBuilder { int rate,channels,format; };
struct AAudioStream { int rate,channels,format; };
static int nativeRate=48000,channels=2,format=AAUDIO_FORMAT_PCM_I16;
static int rejectRate,rejectI16,failStart,writeError,zeroWrite,partial,opens,builders,streams,logs;
static int16_t captured[800000];static int capturedFrames;
void OsAndroid_Log(const char* text){assert(text && *text);++logs;}
const char* AAudio_convertResultToText(aaudio_result_t code){(void)code;return "mock device result";}
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** b){*b=calloc(1,sizeof **b);++builders;return 0;}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder* b){free(b);--builders;return 0;}
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder* b,int32_t v){(void)b;(void)v;}
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder* b,int32_t v){(void)b;(void)v;}
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder* b,int32_t v){b->rate=v;}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder* b,int32_t v){b->channels=v;}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder* b,int32_t v){b->format=v;}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b,AAudioStream** s){
 ++opens;if((rejectRate && b->rate) || (rejectI16 && b->format==AAUDIO_FORMAT_PCM_I16))return -2;
 *s=malloc(sizeof **s);**s=(AAudioStream){nativeRate,channels,format};++streams;return 0;
}
int32_t AAudioStream_getSampleRate(AAudioStream* s){return s->rate;}
int32_t AAudioStream_getChannelCount(AAudioStream* s){return s->channels;}
int32_t AAudioStream_getFormat(AAudioStream* s){return s->format;}
aaudio_result_t AAudioStream_requestStart(AAudioStream* s){(void)s;if(failStart){--failStart;return -3;}return 0;}
aaudio_result_t AAudioStream_close(AAudioStream* s){free(s);--streams;return 0;}
aaudio_result_t AAudioStream_write(AAudioStream* s,const void* data,int32_t count,int64_t timeout){
 int i,c,n=partial && count>partial?partial:count;assert(timeout>0);
 if(writeError)return writeError;
 if(zeroWrite)return 0;
 assert(capturedFrames+n<=400000);
 for(i=0;i<n;i++)for(c=0;c<2;c++){
  int index=i*s->channels+(s->channels==1?0:c);
  captured[2*(capturedFrames+i)+c]=s->format==AAUDIO_FORMAT_PCM_FLOAT?(int16_t)(((const float*)data)[index]*32768.0f):((const int16_t*)data)[index];
 }
 capturedFrames+=n;return n;
}
static void Reset(void){assert(!builders && !streams);nativeRate=48000;channels=2;format=AAUDIO_FORMAT_PCM_I16;rejectRate=rejectI16=failStart=writeError=zeroWrite=partial=opens=capturedFrames=logs=0;}
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
 Reset();nativeRate=target;
 for(i=0;i<frames;i++){input[2*i]=(int16_t)(12000*sin(6.283185307179586*1000*i/source));input[2*i+1]=(int16_t)-input[2*i];}
 assert(AndroidAudio_Open(&out,source,frames,1));assert(AndroidAudio_Write(&out,input,frames));
 assert(capturedFrames==target/5);expected=malloc((size_t)capturedFrames*4);memcpy(expected,captured,(size_t)capturedFrames*4);AndroidAudio_Close(&out);
 capturedFrames=0;assert(AndroidAudio_Open(&out,source,frames,1));
 for(offset=0;offset<frames;){int n=(offset%997)+1;if(n>frames-offset)n=frames-offset;assert(AndroidAudio_Write(&out,input+2*offset,n));offset+=n;}
 assert(capturedFrames==target/5 && memcmp(expected,captured,(size_t)capturedFrames*4)==0);
 {int crossings=0;for(i=1;i<capturedFrames;i++)if(captured[2*(i-1)]<0 && captured[2*i]>=0)++crossings;assert(crossings>=198 && crossings<=200);}
 if(source==target)assert(memcmp(input,captured,(size_t)frames*4)==0);
 AndroidAudio_Close(&out);free(input);free(expected);
}
static void Negotiation(void){
 AndroidAudioOutput_t out={0};int16_t input[882*2];int i;
 Reset();rejectRate=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(opens==2 && out.rate==48000 && logs>0);AndroidAudio_Close(&out);
 Reset();rejectI16=1;format=AAUDIO_FORMAT_PCM_FLOAT;channels=1;
 assert(AndroidAudio_Open(&out,44100,882,1));assert(opens==3);
 for(i=0;i<882;i++){input[2*i]=30000;input[2*i+1]=-10000;}
 partial=17;assert(AndroidAudio_Write(&out,input,882));assert(capturedFrames==960);
 for(i=0;i<capturedFrames*2;i++)assert(captured[i]==10000);
 writeError=-899;assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 Reset();channels=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(AndroidAudio_Write(&out,input,882));
 for(i=0;i<capturedFrames*2;i++)assert(captured[i]==10000);
 zeroWrite=1;assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 Reset();format=AAUDIO_FORMAT_PCM_FLOAT;assert(AndroidAudio_Open(&out,44100,882,1));assert(AndroidAudio_Write(&out,input,882));
 for(i=0;i<capturedFrames;i++){assert(captured[2*i]==30000);assert(captured[2*i+1]==-10000);}AndroidAudio_Close(&out);
 Reset();failStart=1;assert(AndroidAudio_Open(&out,44100,882,1));assert(opens==2);AndroidAudio_Close(&out);
 Reset();format=99;assert(!AndroidAudio_Open(&out,44100,882,1));assert(opens==5 && logs>0);AndroidAudio_Close(&out);
 Reset();nativeRate=0;assert(!AndroidAudio_Open(&out,44100,882,1));assert(opens==5);AndroidAudio_Close(&out);
 Reset();assert(!AndroidAudio_Open(&out,0,882,1));assert(!AndroidAudio_Open(&out,44100,0,1));
 Reset();assert(AndroidAudio_Open(&out,44100,1,1));assert(!AndroidAudio_Write(&out,input,882));AndroidAudio_Close(&out);
 Reset();
}
int main(void){Viewports();StreamContinuity(44100,48000);StreamContinuity(48000,44100);StreamContinuity(44100,44100);StreamContinuity(8000,384000);Negotiation();puts("Android output: scaling policy, negotiation, continuous resampling, duration/pitch, mono/float, partial writes and recovery passed");return 0;}
