/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 * movieav.c - local-file FFmpeg decoding with bounded queues. */
#ifdef BGI_HAVE_FFMPEG
#include "bgi/movieav.h"
#include "bgi/file.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_SLOTS 3
#define AUDIO_RATE 44100
#define AUDIO_FRAMES (AUDIO_RATE * 2)
#define MAX_PIXELS (4096 * 2160)
#define MAX_MOVIE_US (24LL * 3600 * 1000000)
#define PACKET_SLOTS 128
#define PACKET_BYTES (32 * 1024 * 1024)

struct MovieAv
{
    AVFormatContext* format;
    AVIOContext* io;
    FileView_t input;
    uint32_t inputPos;
    AVCodecContext *video,*audio;
    AVRational timeBase[2];
    struct SwsContext* scaler;
    SwrContext* resampler;
    AVChannelLayout inputLayout;
    int inputRate,inputFormat;
    int videoIndex,audioIndex,width,height;
    int64_t originUs,nextVideoUs,nextAudio,endUs,deadline;
    double frameUs;
    uint8_t* pictures[VIDEO_SLOTS];
    int64_t pts[VIDEO_SLOTS];
    int vRead,vWrite,vCount;
    int16_t *ring,*pcm;
    int pcmCapacity;
    int64_t aRead,aWrite;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    AVPacket* packets[2][PACKET_SLOTS];
    int pRead[2],pWrite[2],pCount[2],pBytes[2],readDone,decodeDone,audioDone;
    pthread_t worker,audioWorker,reader;
    int thread,stop,done,playing,failed;
    char error[256];
};

static int Stopped(MovieAv_t* m)
{
    return __atomic_load_n(&m->stop,__ATOMIC_RELAXED);
}

static int Interrupt(void* user)
{
    MovieAv_t* m=user;
    return Stopped(m)||(m->deadline&&av_gettime_relative()>m->deadline);
}

static void ErrorText(char* out,size_t size,const char* operation,int code)
{
    char message[128];av_strerror(code,message,sizeof message);
    if(out&&size)snprintf(out,size,"%s: %s",operation,message);
}

static int OpenCodec(MovieAv_t* m,int type,int* index,AVCodecContext** out)
{
    const AVCodec* codec=NULL;
    int i=av_find_best_stream(m->format,type,-1,-1,&codec,0),r;
    if(i<0)return i;
    *out=avcodec_alloc_context3(codec);
    if(!*out)return AVERROR(ENOMEM);
    if((r=avcodec_parameters_to_context(*out,m->format->streams[i]->codecpar))<0)return r;
    (*out)->thread_count=2;
    (*out)->max_pixels=MAX_PIXELS;
    if((r=avcodec_open2(*out,codec,NULL))<0)return r;
    *index=i;return 0;
}

static int ReadInput(void* user, uint8_t* buffer, int requested)
{
    MovieAv_t* m = user;
    uint32_t count = BGI_MIN((uint32_t)requested, m->input.size - m->inputPos);
    if(!count) return AVERROR_EOF;
    count = File_Read(&m->input.file, buffer, count);
    if(!count) return AVERROR(EIO);
    m->inputPos += count;
    return (int)count;
}

static int64_t SeekInput(void* user, int64_t offset, int whence)
{
    MovieAv_t* m = user;
    int64_t base;
    switch(whence & ~AVSEEK_FORCE)
    {
        case AVSEEK_SIZE: return m->input.size;
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = m->inputPos; break;
        case SEEK_END: base = m->input.size; break;
        default: return AVERROR(EINVAL);
    }
    if(offset < -base || offset > (int64_t)m->input.size - base)
        return AVERROR(EINVAL);
    if(!File_Seek(&m->input.file, m->input.offset + (uint32_t)(base + offset)))
        return AVERROR(EIO);
    m->inputPos = (uint32_t)(base + offset);
    return m->inputPos;
}

static MovieAv_t* MovieAv_OpenInput(const char* path, FileView_t* view, int audio, MovieAvInfo_t* info,char* error,size_t size)
{
    MovieAv_t* m;AVDictionary* options=NULL;int r;
    if((!path&&!view)||!info){ErrorText(error,size,"open movie",AVERROR(EINVAL));return NULL;}
    m=calloc(1,sizeof *m);
    if(!m){ErrorText(error,size,"allocate movie",AVERROR(ENOMEM));return NULL;}
    m->videoIndex=m->audioIndex=-1;m->frameUs=1000000.0/30;
    r=pthread_mutex_init(&m->lock,NULL);
    if(r){ErrorText(error,size,"movie mutex",AVERROR(r));free(m);return NULL;}
    r=pthread_cond_init(&m->changed,NULL);
    if(r){ErrorText(error,size,"movie condition",AVERROR(r));pthread_mutex_destroy(&m->lock);free(m);return NULL;}
    m->format=avformat_alloc_context();
    if(!m->format){r=AVERROR(ENOMEM);goto fail;}
    m->deadline=av_gettime_relative()+10000000;
    m->format->interrupt_callback=(AVIOInterruptCB){Interrupt,m};
    m->format->probesize=8*1024*1024;m->format->max_analyze_duration=5000000;
    if(view)
    {
        uint8_t* buffer;
        if(view->size > UINT32_MAX - view->offset || !File_Seek(&view->file, view->offset))
        {
            r = AVERROR(EIO);
            goto fail;
        }
        buffer = av_malloc(32768);
        if(!buffer){r=AVERROR(ENOMEM);goto fail;}
        m->io = avio_alloc_context(buffer, 32768, 0, m, ReadInput, NULL, SeekInput);
        if(!m->io){av_free(buffer);r=AVERROR(ENOMEM);goto fail;}
        m->input = *view;
        File_Ctor(&view->file);
        m->format->pb = m->io;
        m->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
    av_dict_set(&options,"protocol_whitelist","file",0);
    r=avformat_open_input(&m->format,path,NULL,&options);av_dict_free(&options);
    if(r<0)goto fail;
    if((r=avformat_find_stream_info(m->format,NULL))<0)goto fail;
    if((r=OpenCodec(m,AVMEDIA_TYPE_VIDEO,&m->videoIndex,&m->video))<0)goto fail;
    if(m->video->width<=0||m->video->height<=0||(int64_t)m->video->width*m->video->height>MAX_PIXELS){r=AVERROR_INVALIDDATA;goto fail;}
    if(audio&&OpenCodec(m,AVMEDIA_TYPE_AUDIO,&m->audioIndex,&m->audio)<0){avcodec_free_context(&m->audio);m->audioIndex=-1;}
    m->timeBase[0]=m->format->streams[m->videoIndex]->time_base;
    if(m->audio)m->timeBase[1]=m->format->streams[m->audioIndex]->time_base;
    if(m->format->start_time!=AV_NOPTS_VALUE)m->originUs=m->format->start_time;
    else
    {
        int have=0;
        for(unsigned i=0;i<m->format->nb_streams;i++)
        {
            AVStream* s=m->format->streams[i];
            if(s->start_time!=AV_NOPTS_VALUE)
            {
                int64_t t=av_rescale_q(s->start_time,s->time_base,AV_TIME_BASE_Q);
                if(!have||t<m->originUs)m->originUs=t;have=1;
            }
        }
    }
    {
        AVRational fps=av_guess_frame_rate(m->format,m->format->streams[m->videoIndex],NULL);
        double f=av_q2d(fps);if(f>=1&&f<=240)m->frameUs=1000000.0/f;
    }
    memset(info,0,sizeof *info);info->width=m->video->width;info->height=m->video->height;info->audio=m->audio!=NULL;
    if(m->format->duration>0&&m->format->duration!=AV_NOPTS_VALUE)
        info->durationMs=(int32_t)(m->format->duration/1000>INT32_MAX?INT32_MAX:m->format->duration/1000);
    m->deadline=0;return m;
fail:
    av_dict_free(&options);ErrorText(error,size,"open movie",r);MovieAv_Close(m);return NULL;
}

MovieAv_t* MovieAv_Open(const char* path,int audio,MovieAvInfo_t* info,char* error,size_t size)
{
    return MovieAv_OpenInput(path, NULL, audio, info, error, size);
}

MovieAv_t* MovieAv_OpenView(FileView_t* view,int audio,MovieAvInfo_t* info,char* error,size_t size)
{
    return MovieAv_OpenInput(NULL, view, audio, info, error, size);
}

static int64_t Timestamp(MovieAv_t* m,AVFrame* frame,int stream,int64_t fallback)
{
    int64_t t=frame->best_effort_timestamp;
    if(t==AV_NOPTS_VALUE)return fallback>=0&&fallback<=MAX_MOVIE_US?fallback:-1;
    t=av_rescale_q(t,m->timeBase[stream==m->videoIndex?0:1],AV_TIME_BASE_Q);
    if(t<m->originUs)return 0;
    if(m->originUs<0&&t>INT64_MAX+m->originUs)return -1;
    t-=m->originUs;
    return t>MAX_MOVIE_US?-1:t;
}

static int VideoFrame(MovieAv_t* m,AVFrame* f)
{
    int slot,r,space;int64_t pts=Timestamp(m,f,m->videoIndex,m->nextVideoUs),duration=(int64_t)m->frameUs;
    if(pts<0)return AVERROR_INVALIDDATA;
    if(f->width<=0||f->height<=0||(int64_t)f->width*f->height>MAX_PIXELS)return AVERROR_INVALIDDATA;
    if(f->duration>0)duration=av_rescale_q(f->duration,m->timeBase[0],AV_TIME_BASE_Q);
    if(duration<=0||duration>10000000)duration=(int64_t)m->frameUs;
    m->nextVideoUs=pts+duration;
    pthread_mutex_lock(&m->lock);
    while(m->vCount==VIDEO_SLOTS&&!Stopped(m))pthread_cond_wait(&m->changed,&m->lock);
    if(Stopped(m)){pthread_mutex_unlock(&m->lock);return AVERROR_EXIT;}
    slot=m->vWrite;pthread_mutex_unlock(&m->lock);
    m->scaler=sws_getCachedContext(m->scaler,f->width,f->height,f->format,m->width,m->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!m->scaler)return AVERROR(ENOMEM);
    space=f->colorspace==AVCOL_SPC_BT709?SWS_CS_ITU709:f->colorspace==AVCOL_SPC_BT2020_NCL?SWS_CS_BT2020:SWS_CS_ITU601;
    sws_setColorspaceDetails(m->scaler,sws_getCoefficients(space),f->color_range==AVCOL_RANGE_JPEG,
                            sws_getCoefficients(space),1,0,1<<16,1<<16);
    {uint8_t* dst[4]={m->pictures[slot],NULL,NULL,NULL};int stride[4]={m->width*4,0,0,0};
     r=sws_scale(m->scaler,(const uint8_t* const*)f->data,f->linesize,0,f->height,dst,stride);}
    if(r!=m->height)return AVERROR_INVALIDDATA;
    pthread_mutex_lock(&m->lock);
    m->pts[slot]=pts;m->vWrite=(slot+1)%VIDEO_SLOTS;m->vCount++;
    if(pts+duration>m->endUs)m->endUs=pts+duration;
    pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);return 0;
}

/* Absolute audio positions preserve leading silence and discontinuities. A
 * late producer drops samples the mixer has already passed, rather than lagging. */
static int AudioPush(MovieAv_t* m,const int16_t* pcm,int count,int64_t position)
{
    int at=0;pthread_mutex_lock(&m->lock);
    while(at<count&&!Stopped(m))
    {
        int64_t skip;int free,n,slot;
        if(m->aWrite<m->aRead)m->aWrite=m->aRead;
        if(position<m->aWrite){skip=m->aWrite-position;if(skip>count-at)skip=count-at;at+=(int)skip;position+=skip;continue;}
        free=AUDIO_FRAMES-(int)(m->aWrite-m->aRead);
        if(free<=0){pthread_cond_wait(&m->changed,&m->lock);continue;}
        slot=(int)(m->aWrite%AUDIO_FRAMES);n=AUDIO_FRAMES-slot;if(n>free)n=free;
        if(position>m->aWrite)
        {
            if(position-m->aWrite<n)n=(int)(position-m->aWrite);
            memset(m->ring+(size_t)slot*2,0,(size_t)n*4);m->aWrite+=n;continue;
        }
        if(n>count-at)n=count-at;
        memcpy(m->ring+(size_t)slot*2,pcm+(size_t)at*2,(size_t)n*4);
        at+=n;position+=n;m->aWrite+=n;pthread_cond_broadcast(&m->changed);
    }
    if(position*1000000/AUDIO_RATE>m->endUs)m->endUs=position*1000000/AUDIO_RATE;
    pthread_mutex_unlock(&m->lock);return Stopped(m)?AVERROR_EXIT:0;
}

static int AudioFrame(MovieAv_t* m,AVFrame* f)
{
    int capacity,n,r;int64_t position,delay;
    if(f->sample_rate<8000||f->sample_rate>384000||f->ch_layout.nb_channels<1||f->ch_layout.nb_channels>16||f->nb_samples<0||f->nb_samples>262144)return AVERROR_INVALIDDATA;
    if(!m->resampler)
    {
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if((r=swr_alloc_set_opts2(&m->resampler,&stereo,AV_SAMPLE_FMT_S16,AUDIO_RATE,&f->ch_layout,f->format,f->sample_rate,0,NULL))<0)return r;
        if((r=swr_init(m->resampler))<0)return r;
        m->inputRate=f->sample_rate;m->inputFormat=f->format;
        if((r=av_channel_layout_copy(&m->inputLayout,&f->ch_layout))<0)return r;
    }
    else if(m->inputRate!=f->sample_rate||m->inputFormat!=f->format||av_channel_layout_compare(&m->inputLayout,&f->ch_layout))return AVERROR_INVALIDDATA;
    capacity=swr_get_out_samples(m->resampler,f->nb_samples);
    if(capacity<0||capacity>AUDIO_RATE*4)return AVERROR_INVALIDDATA;
    if(capacity>m->pcmCapacity)
    {
        int16_t* next=av_realloc(m->pcm,(size_t)capacity*4);if(!next)return AVERROR(ENOMEM);m->pcm=next;m->pcmCapacity=capacity;
    }
    delay=swr_get_delay(m->resampler,f->sample_rate);
    position=Timestamp(m,f,m->audioIndex,m->nextAudio*1000000/AUDIO_RATE);
    if(position<0)return AVERROR_INVALIDDATA;
    position=av_rescale(position,AUDIO_RATE,1000000)-av_rescale(delay,AUDIO_RATE,f->sample_rate);
    if(position<0)position=0;
    if(llabs(position-m->nextAudio)>AUDIO_RATE/100)m->nextAudio=position;
    {uint8_t* output=(uint8_t*)m->pcm;n=swr_convert(m->resampler,&output,capacity,(const uint8_t**)f->extended_data,f->nb_samples);}
    if(n<0)return n;r=AudioPush(m,m->pcm,n,m->nextAudio);m->nextAudio+=n;return r;
}

static int Drain(MovieAv_t* m,AVCodecContext* codec,AVFrame* frame,int video)
{
    int r;
    while((r=avcodec_receive_frame(codec,frame))>=0)
    {
        int result=video?VideoFrame(m,frame):AudioFrame(m,frame);av_frame_unref(frame);if(result<0)return result;
    }
    return r==AVERROR(EAGAIN)||r==AVERROR_EOF?0:r;
}

static int Packet(MovieAv_t* m,AVCodecContext* codec,AVPacket* packet,AVFrame* frame,int video)
{
    int r=avcodec_send_packet(codec,packet);
    if(r==AVERROR(EAGAIN)){if((r=Drain(m,codec,frame,video))<0)return r;r=avcodec_send_packet(codec,packet);}
    return r<0?r:Drain(m,codec,frame,video);
}

static void Fail(MovieAv_t* m,int code)
{
    pthread_mutex_lock(&m->lock);
    if(!Stopped(m)){m->failed=1;ErrorText(m->error,sizeof m->error,"decode movie",code);}
    __atomic_store_n(&m->stop,1,__ATOMIC_RELAXED);
    pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);
}

/* Demux must not block behind three decoded video frames: MPEG-PS can
 * interleave a substantial run of video packets before the next audio packet.
 * Separate bounded compressed queues keep audio supplied during that run. */
static void* Reader(void* user)
{
    MovieAv_t* m=user;int r=0;
    while(!Stopped(m))
    {
        AVPacket* packet=av_packet_alloc();int q;
        if(!packet){r=AVERROR(ENOMEM);break;}
        r=av_read_frame(m->format,packet);
        if(r<0){av_packet_free(&packet);break;}
        q=packet->stream_index==m->videoIndex?0:m->audio&&packet->stream_index==m->audioIndex?1:-1;
        if(q<0){av_packet_free(&packet);continue;}
        if(packet->size<0||packet->size>PACKET_BYTES){av_packet_free(&packet);r=AVERROR_INVALIDDATA;break;}
        pthread_mutex_lock(&m->lock);
        while((m->pCount[q]==PACKET_SLOTS||m->pBytes[q]>PACKET_BYTES-packet->size)&&!Stopped(m))pthread_cond_wait(&m->changed,&m->lock);
        if(Stopped(m)){pthread_mutex_unlock(&m->lock);av_packet_free(&packet);break;}
        m->packets[q][m->pWrite[q]]=packet;m->pWrite[q]=(m->pWrite[q]+1)%PACKET_SLOTS;
        m->pCount[q]++;m->pBytes[q]+=packet->size;
        pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);
    }
    if(r<0&&r!=AVERROR_EOF&&!Stopped(m))Fail(m,r);
    pthread_mutex_lock(&m->lock);m->readDone=1;pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);return NULL;
}

static void* Decode(MovieAv_t* m,int video)
{
    AVFrame* frame=av_frame_alloc();AVCodecContext* codec=video?m->video:m->audio;
    int q=video?0:1,r=0,badPackets=0;
    if(!frame){r=AVERROR(ENOMEM);goto end;}
    while(!Stopped(m))
    {
        AVPacket* packet;pthread_mutex_lock(&m->lock);
        while(!m->pCount[q]&&!m->readDone&&!Stopped(m))pthread_cond_wait(&m->changed,&m->lock);
        if(Stopped(m)||!m->pCount[q]){pthread_mutex_unlock(&m->lock);r=0;break;}
        packet=m->packets[q][m->pRead[q]];m->packets[q][m->pRead[q]]=NULL;
        m->pRead[q]=(m->pRead[q]+1)%PACKET_SLOTS;m->pCount[q]--;m->pBytes[q]-=packet->size;
        pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);
        r=Packet(m,codec,packet,frame,video);av_packet_free(&packet);
        if(r<0){if(r!=AVERROR_INVALIDDATA||++badPackets>=20)break;}else badPackets=0;
    }
    if(r>=0&&!Stopped(m))
    {
        r=Packet(m,codec,NULL,frame,video);
        if(r>=0&&!video&&m->resampler)
        {
            int n;uint8_t* output=(uint8_t*)m->pcm;
            while((n=swr_convert(m->resampler,&output,m->pcmCapacity,NULL,0))>0)
            {if((r=AudioPush(m,m->pcm,n,m->nextAudio))<0)break;m->nextAudio+=n;}
            if(n<0)r=n;
        }
    }
end:
    av_frame_free(&frame);
    if(r<0&&!Stopped(m))Fail(m,r);
    pthread_mutex_lock(&m->lock);
    if(!video)m->audioDone=1;
    if(++m->decodeDone==(m->audio?2:1))m->done=1;
    pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);return NULL;
}
static void* Worker(void* user){return Decode(user,1);}
static void* AudioWorker(void* user){return Decode(user,0);}

int MovieAv_Start(MovieAv_t* m,int width,int height)
{
    if(!m||m->thread||m->width||width<=0||height<=0||(int64_t)width*height>MAX_PIXELS)return 0;
    m->width=width;m->height=height;
    for(int i=0;i<VIDEO_SLOTS;i++){m->pictures[i]=av_malloc((size_t)width*height*4);if(!m->pictures[i])return 0;}
    if(m->audio){m->ring=calloc(AUDIO_FRAMES,4);if(!m->ring)return 0;}
    if(pthread_create(&m->worker,NULL,Worker,m))return 0;
    m->thread=1;
    if(m->audio){if(pthread_create(&m->audioWorker,NULL,AudioWorker,m))return 0;m->thread|=2;}
    if(pthread_create(&m->reader,NULL,Reader,m))return 0;
    m->thread|=4;return 1;
}

int MovieAv_Ready(MovieAv_t* m)
{
    int ready;pthread_mutex_lock(&m->lock);
    ready=m->failed||m->done||(m->vCount>0&&(!m->audio||m->aWrite>=AUDIO_RATE/10||m->audioDone));
    pthread_mutex_unlock(&m->lock);return ready;
}

void MovieAv_Begin(MovieAv_t* m)
{
    pthread_mutex_lock(&m->lock);m->playing=1;pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);
}

int MovieAv_Frame(MovieAv_t* m,int64_t clockUs,uint8_t* pixels)
{
    int slot=-1;pthread_mutex_lock(&m->lock);
    while(m->vCount&&m->pts[m->vRead]<=clockUs)
    {slot=m->vRead;m->vRead=(m->vRead+1)%VIDEO_SLOTS;m->vCount--;}
    if(slot>=0)memcpy(pixels,m->pictures[slot],(size_t)m->width*m->height*4);
    pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);return slot>=0;
}

int64_t MovieAv_Clock(MovieAv_t* m,int64_t wallUs,int audioMaster)
{
    int64_t result=wallUs;pthread_mutex_lock(&m->lock);
    if(m->audio&&audioMaster)result=m->aRead*1000000/AUDIO_RATE;
    else if(m->audio&&m->aRead<wallUs*AUDIO_RATE/1000000){m->aRead=wallUs*AUDIO_RATE/1000000;pthread_cond_broadcast(&m->changed);}
    pthread_mutex_unlock(&m->lock);return result;
}

void MovieAv_Mix(MovieAv_t* m,int32_t* stereo,int frames,int gain16)
{
    pthread_mutex_lock(&m->lock);
    if(m->audio&&m->playing)
    {
        int64_t available=m->aWrite-m->aRead;int n=available<=0?0:available<frames?(int)available:frames;
        for(int i=0;i<n;i++)for(int c=0;c<2;c++)stereo[i*2+c]+=(int32_t)((int64_t)m->ring[(size_t)((m->aRead+i)%AUDIO_FRAMES)*2+c]*gain16/65536);
        m->aRead+=frames;pthread_cond_broadcast(&m->changed);
    }
    pthread_mutex_unlock(&m->lock);
}

int MovieAv_Done(MovieAv_t* m,int64_t clockUs)
{
    int done;pthread_mutex_lock(&m->lock);done=m->failed||(m->done&&!m->vCount&&(!m->audio||m->aRead>=m->aWrite)&&clockUs>=m->endUs);pthread_mutex_unlock(&m->lock);return done;
}

int MovieAv_Error(MovieAv_t* m,char* error,size_t size)
{
    int failed;pthread_mutex_lock(&m->lock);failed=m->failed;if(failed&&error&&size)snprintf(error,size,"%s",m->error);pthread_mutex_unlock(&m->lock);return failed;
}

void MovieAv_Close(MovieAv_t* m)
{
    if(!m)return;
    __atomic_store_n(&m->stop,1,__ATOMIC_RELAXED);pthread_mutex_lock(&m->lock);pthread_cond_broadcast(&m->changed);pthread_mutex_unlock(&m->lock);
    if(m->thread&4)pthread_join(m->reader,NULL);
    if(m->thread&1)pthread_join(m->worker,NULL);
    if(m->thread&2)pthread_join(m->audioWorker,NULL);
    for(int q=0;q<2;q++)for(int i=0;i<PACKET_SLOTS;i++)av_packet_free(&m->packets[q][i]);
    for(int i=0;i<VIDEO_SLOTS;i++)av_free(m->pictures[i]);
    free(m->ring);av_free(m->pcm);sws_freeContext(m->scaler);swr_free(&m->resampler);av_channel_layout_uninit(&m->inputLayout);
    avcodec_free_context(&m->video);avcodec_free_context(&m->audio);avformat_close_input(&m->format);
    if(m->io)
    {
        av_freep(&m->io->buffer);
        avio_context_free(&m->io);
        File_Close(&m->input.file);
    }
    pthread_cond_destroy(&m->changed);pthread_mutex_destroy(&m->lock);free(m);
}
#endif
