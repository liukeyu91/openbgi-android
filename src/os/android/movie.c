/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 * movie.c - FFmpeg movies presented through the logical game canvas. */
#include "android_internal.h"
#include "bgi/movieav.h"
#include "bgi/gfx.h"
#include "bgi/snd/wavemaster.h"
#include <math.h>

static struct
{
    MovieAv_t* decoder;
    MovieAvInfo_t info;
    uint8_t* picture;
    int x,y,w,h,started,notified,hooked;
    int64_t startUs;
    int gain16;
} gMovie;
static int gForeground,gStatus;

void OsAndroid_MovieForeground(int resumed){__atomic_store_n(&gForeground,resumed,__ATOMIC_RELAXED);}
int OsAndroid_MovieStatus(void){return __atomic_load_n(&gStatus,__ATOMIC_RELAXED);}
static void Status(int status){__atomic_store_n(&gStatus,status,__ATOMIC_RELAXED);}

static void Mix(void* user,int32_t* acc,int frames)
{
    if(__atomic_load_n(&gForeground,__ATOMIC_RELAXED))
        MovieAv_Mix(user,acc,frames,__atomic_load_n(&gMovie.gain16,__ATOMIC_RELAXED));
}

void OS_MovieStop(void)
{
    if(gMovie.hooked)Wm_SetExternalMix(NULL,NULL);
    MovieAv_Close(gMovie.decoder);free(gMovie.picture);
    memset(&gMovie,0,sizeof gMovie);
    if(OsAndroid_MovieStatus()>0)Status(0);
}

int OS_MovieOpen(const char* path)
{
    char native[PATH_MAX],error[256];int audio;
    OS_MovieStop();Status(0);OS_PathNative(path,native,sizeof native);
    Wm_EnsureMixer();
    audio=Wm_MixRunning();
    gMovie.decoder=MovieAv_Open(native,audio,&gMovie.info,error,sizeof error);
    if(!gMovie.decoder){OsAndroid_Log(error);Status(-1);return OS_MOVIE_FAILED;}
    {char log[256];snprintf(log,sizeof log,"Movie opened: %dx%d, length=%dms, audio=%d",gMovie.info.width,gMovie.info.height,gMovie.info.durationMs,gMovie.info.audio);OsAndroid_Log(log);}
    return OS_MOVIE_OK;
}

void OS_MovieSetVolume(int32_t volume)
{
    int gain=volume<=-10000?0:volume>=0?65536:(int)(pow(10.0,volume/2000.0)*65536.0);
    __atomic_store_n(&gMovie.gain16,gain,__ATOMIC_RELAXED);
}

int OS_MovieStart(int x,int y,int w,int h,int32_t volume,int32_t* lengthMs)
{
    if(!gMovie.decoder||w<=0||h<=0||(int64_t)w*h>4096*2160)return 0;
    gMovie.picture=malloc((size_t)w*h*4);
    if(!gMovie.picture||!MovieAv_Start(gMovie.decoder,w,h))
    {OsAndroid_Log("Movie decoder could not start");OS_MovieStop();Status(-1);return 0;}
    gMovie.x=x;gMovie.y=y;gMovie.w=w;gMovie.h=h;OS_MovieSetVolume(volume);
    if(gMovie.info.audio){Wm_SetExternalMix(Mix,gMovie.decoder);gMovie.hooked=1;}
    *lengthMs=gMovie.info.durationMs;Status(1);return 1;
}

int OS_MovieIsRunning(void){return gMovie.decoder&&gMovie.picture&&!gMovie.notified;}

/* Runs on the engine thread before its ordinary surface present. The return
 * value is delivered through the same completion handler as DirectShow. */
int OsAndroid_MovieTick(void)
{
    int64_t now,clock;char error[256];
    if(!OS_MovieIsRunning())return 0;
    now=(int64_t)(OsAndroid_ClockNs()/1000);
    if(!gMovie.started)
    {
        if(!MovieAv_Ready(gMovie.decoder))return 0;
        gMovie.startUs=now;gMovie.started=1;MovieAv_Begin(gMovie.decoder);
    }
    clock=MovieAv_Clock(gMovie.decoder,now-gMovie.startUs,gMovie.hooked&&Wm_MixRunning());
    if(gMovie.hooked&&Wm_MixRunning()){
        clock-=OsAndroid_AudioDelayUs();if(clock<0)clock=0;
    }
    if(MovieAv_Frame(gMovie.decoder,clock,gMovie.picture))
    {
        OsSurface_t surface={gMovie.picture,gMovie.w*4,gMovie.w,gMovie.h,32};
        OsDc_t* dc=OS_WindowGetDc();
        /* Movie_Play already mapped this rectangle through display scaling. */
        OS_StretchToWindow(dc,gMovie.x,gMovie.y,gMovie.w,gMovie.h,&surface,NULL);
        OS_WindowReleaseDc(dc);
    }
    if(MovieAv_Done(gMovie.decoder,clock))
    {
        if(MovieAv_Error(gMovie.decoder,error,sizeof error)){OsAndroid_Log(error);Status(-1);}
        else OsAndroid_Log("Movie completed");
        gMovie.notified=1;return 1;
    }
    return 0;
}

int OsAndroid_MovieSkip(void)
{
    if(!OS_MovieIsRunning())return 0;
    /* Ignore the press that may have initiated this movie. */
    if(!gMovie.started||(int64_t)(OsAndroid_ClockNs()/1000)-gMovie.startUs<200000)return 0;
    OsAndroid_Log("Movie skipped by input");Movie_OnEvent(1);return 1;
}
