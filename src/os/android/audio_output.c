/* SPDX-License-Identifier: GPL-2.0-only
 * AAudio negotiation and streaming conversion, independent of the VM and JNI.
 */
#include "audio_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void OsAndroid_Log(const char* text);

static void Failure(const char* operation,aaudio_result_t result)
{
    char text[256];
    snprintf(text,sizeof text,"AAudio %s failed: %d (%s)",operation,(int)result,AAudio_convertResultToText(result));
    OsAndroid_Log(text);
}

void AndroidAudio_Close(AndroidAudioOutput_t* out)
{
    if(out->stream)AAudioStream_close(out->stream);
    free(out->pcm);free(out->converted);
    memset(out,0,sizeof *out);
}

int AndroidAudio_Open(AndroidAudioOutput_t* out,int sourceRate,int period,int verbose)
{
    const aaudio_format_t formats[]={AAUDIO_FORMAT_PCM_I16,AAUDIO_FORMAT_PCM_I16,
        AAUDIO_FORMAT_PCM_FLOAT,AAUDIO_FORMAT_PCM_I16,AAUDIO_FORMAT_PCM_FLOAT};
    int attempt;
    AndroidAudio_Close(out);
    if(sourceRate<1 || sourceRate>384000 || period<1 || period>sourceRate)return 0;
    for(attempt=0;attempt<5;attempt++){
        AAudioStreamBuilder* builder=NULL;
        AAudioStream* stream=NULL;
        int requestedRate=attempt==0?sourceRate:AAUDIO_UNSPECIFIED;
        int requestedChannels=attempt<3?2:1;
        aaudio_result_t result=AAudio_createStreamBuilder(&builder);
        if(result!=AAUDIO_OK){if(verbose)Failure("create builder",result);continue;}
        AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setUsage(builder,AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setSampleRate(builder,requestedRate);
        AAudioStreamBuilder_setChannelCount(builder,requestedChannels);
        AAudioStreamBuilder_setFormat(builder,formats[attempt]);
        result=AAudioStreamBuilder_openStream(builder,&stream);
        AAudioStreamBuilder_delete(builder);
        if(result!=AAUDIO_OK){
            if(verbose){char text[256];snprintf(text,sizeof text,"AAudio open request: rate=%d channels=%d format=%d",requestedRate,requestedChannels,(int)formats[attempt]);OsAndroid_Log(text);Failure("open",result);}
            if(stream)AAudioStream_close(stream);
            continue;
        }
        out->stream=stream;out->sourceRate=sourceRate;
        out->rate=AAudioStream_getSampleRate(stream);out->channels=AAudioStream_getChannelCount(stream);out->format=AAudioStream_getFormat(stream);
        if(out->rate<8000 || out->rate>384000 || (out->channels!=1 && out->channels!=2) ||
            (out->format!=AAUDIO_FORMAT_PCM_I16 && out->format!=AAUDIO_FORMAT_PCM_FLOAT)){
            if(verbose){char text[192];snprintf(text,sizeof text,"AAudio unsupported negotiated output: rate=%d channels=%d format=%d",out->rate,out->channels,(int)out->format);OsAndroid_Log(text);}
            AndroidAudio_Close(out);continue;
        }
        out->capacity=(int)(((uint64_t)period*out->rate+sourceRate-1)/sourceRate)+1;
        out->pcm=malloc((size_t)out->capacity*2*sizeof *out->pcm);
        if(out->format==AAUDIO_FORMAT_PCM_FLOAT || out->channels==1)
            out->converted=malloc((size_t)out->capacity*out->channels*(out->format==AAUDIO_FORMAT_PCM_FLOAT?sizeof(float):sizeof(int16_t)));
        if(!out->pcm || ((out->format==AAUDIO_FORMAT_PCM_FLOAT || out->channels==1) && !out->converted)){
            if(verbose)OsAndroid_Log("AAudio output buffer allocation failed");
            AndroidAudio_Close(out);return 0;
        }
        result=AAudioStream_requestStart(stream);
        if(result!=AAUDIO_OK){if(verbose)Failure("start",result);AndroidAudio_Close(out);continue;}
        {char text[192];snprintf(text,sizeof text,"AAudio output started: mixer=%d Hz stereo I16 -> device=%d Hz channels=%d format=%s%s",sourceRate,out->rate,out->channels,out->format==AAUDIO_FORMAT_PCM_FLOAT?"float":"I16",sourceRate!=out->rate?" (resampling)":"");OsAndroid_Log(text);}
        return 1;
    }
    if(verbose)OsAndroid_Log("AAudio output unavailable; retrying while foregrounded (mixer clock continues)");
    return 0;
}

/* One-sample causal interpolation avoids a lookahead buffer and keeps block
 * boundaries continuous. Equal rates bypass conversion bit-for-bit. */
static int Resample(AndroidAudioOutput_t* out,const int16_t* input,int frames)
{
    int i,n=0,c;
    uint64_t span=(uint64_t)frames*out->rate;
    uint64_t needed=span>out->phase?(span-out->phase+out->sourceRate-1)/out->sourceRate:0;
    if(needed>(uint64_t)out->capacity)return -1;
    if(out->sourceRate==out->rate){
        memcpy(out->pcm,input,(size_t)frames*2*sizeof *input);return frames;
    }
    for(i=0;i<frames;i++){
        const int16_t* current=input+2*i;
        if(!out->havePrevious){out->previous[0]=current[0];out->previous[1]=current[1];out->havePrevious=1;}
        while(out->phase<(uint64_t)out->rate){
            for(c=0;c<2;c++){
                int64_t value=(int64_t)out->previous[c]*(int64_t)(out->rate-out->phase)+(int64_t)current[c]*(int64_t)out->phase;
                out->pcm[2*n+c]=(int16_t)(value/out->rate);
            }
            ++n;out->phase+=out->sourceRate;
        }
        out->phase-=out->rate;out->previous[0]=current[0];out->previous[1]=current[1];
    }
    return n;
}

static int64_t NowNs(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000000000+now.tv_nsec;
}

int AndroidAudio_Write(AndroidAudioOutput_t* out,const int16_t* stereo,int frames)
{
    int n,done=0,i,c,bytes;
    const void* data;
    int64_t deadline;
    if(!out->stream || frames<0)return 0;
    n=Resample(out,stereo,frames);
    if(n<0){OsAndroid_Log("AAudio resampling buffer capacity exceeded");return 0;}
    data=out->pcm;bytes=out->channels*(int)sizeof(int16_t);
    if(out->format==AAUDIO_FORMAT_PCM_FLOAT){
        float* converted=out->converted;
        for(i=0;i<n;i++)for(c=0;c<out->channels;c++){
            int sample=out->channels==1?((int)out->pcm[2*i]+out->pcm[2*i+1])/2:out->pcm[2*i+c];
            converted[i*out->channels+c]=sample/32768.0f;
        }
        data=converted;bytes=out->channels*(int)sizeof(float);
    }else if(out->channels==1){
        int16_t* converted=out->converted;
        for(i=0;i<n;i++)converted[i]=(int16_t)(((int)out->pcm[2*i]+out->pcm[2*i+1])/2);
        data=converted;
    }
    deadline=NowNs()+40000000;
    while(done<n){
        int64_t remaining=deadline-NowNs();
        aaudio_result_t written;
        if(remaining<=0){OsAndroid_Log("AAudio write timed out; reopening output");return 0;}
        written=AAudioStream_write(out->stream,(const char*)data+(size_t)done*bytes,n-done,remaining);
        if(written<=0){if(written<0)Failure("write",written);else OsAndroid_Log("AAudio write returned no frames; reopening output");return 0;}
        if(written>n-done){OsAndroid_Log("AAudio returned an invalid write count");return 0;}
        done+=written;
    }
    return 1;
}
