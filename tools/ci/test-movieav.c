/* SPDX-License-Identifier: GPL-2.0-only
 * Exercise the actual threaded decoder with encoded synthetic files. */
#include "bgi/movieav.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static void Wait(int ms){struct timespec t={ms/1000,(ms%1000)*1000000L};nanosleep(&t,NULL);}
static int64_t Now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}
static MovieAv_t* Open(const char* path,int audio,MovieAvInfo_t* info)
{
    char error[256];MovieAv_t* m=MovieAv_Open(path,audio,info,error,sizeof error);
    if(!m){fprintf(stderr,"%s: %s\n",path,error);abort();}
    assert(info->width==160&&info->height==90);
    assert(!MovieAv_Start(m,0,46));assert(!MovieAv_Start(m,4096,2161));
    assert(MovieAv_Start(m,80,46));
    assert(!MovieAv_Start(m,80,46));
    for(int i=0;!MovieAv_Ready(m)&&i<5000;i++)Wait(1);
    assert(MovieAv_Ready(m));return m;
}
static void Play(const char* path,int audio,int gain,int fallback,int vfr)
{
    MovieAvInfo_t info;MovieAv_t* m=Open(path,audio,&info);
    uint8_t picture[80*46*4];int32_t mix[441*2];char error[256];
    int shown=0,nonzero=0,crossings=0,previous=0,done=0;int64_t clock=0;double energy=0;
    MovieAv_Begin(m);
    /* No callbacks while backgrounded: audio master must stay frozen. */
    assert(MovieAv_Clock(m,0,1)==0);Wait(40);assert(MovieAv_Clock(m,0,1)==0);
    for(int step=0;step<800;step++)
    {
        memset(mix,0,sizeof mix);
        if(!fallback)MovieAv_Mix(m,mix,441,gain);
        clock=MovieAv_Clock(m,step*10000LL,!fallback&&audio);
        if(MovieAv_Frame(m,clock,picture))
        {
            int varied=0;
            for(size_t i=0;i<sizeof picture;i+=4){assert(picture[i+3]==255);if(picture[i]!=picture[i+1])varied=1;}
            assert(varied);shown++;
        }
        for(int i=0;i<441;i++)
        {
            int x=mix[i*2];assert(x==mix[i*2+1]);
            if(x){nonzero++;energy+=(double)x*x;}
            if(previous<0&&x>=0)crossings++;previous=x;
        }
        if(MovieAv_Done(m,clock)){done=1;break;}
        Wait(10);
    }
    if(MovieAv_Error(m,error,sizeof error)){fprintf(stderr,"%s: %s\n",path,error);abort();}
    fprintf(stderr,"decoded %s: audio=%d frames=%d clock=%lld nonzero=%d crossings=%d rms=%.0f\n",path,info.audio,shown,(long long)clock,nonzero,crossings,nonzero?sqrt(energy/nonzero):0);
    assert(done&&clock>1700000&&clock<2800000);
    assert(shown>=(vfr?15:35)&&shown<=55);
    if(audio&&gain&&!fallback){assert(nonzero>50000);assert(sqrt(energy/nonzero)>500);assert(crossings>1500&&crossings<2500);}
    else assert(nonzero==0);
    printf("movie: %s audio=%d gain=%d fallback=%d frames=%d end=%lldms samples=%d\n",path,audio,gain,fallback,shown,(long long)clock/1000,nonzero);
    MovieAv_Close(m);
}
static void Probe(const char* path)
{
    MovieAvInfo_t info;char error[256];int frames=0;int64_t energy=0;
    MovieAv_t* m=MovieAv_Open(path,1,&info,error,sizeof error);
    if(!m){fprintf(stderr,"%s\n",error);abort();}
    assert(MovieAv_Start(m,320,180));
    for(int i=0;!MovieAv_Ready(m)&&i<5000;i++)Wait(1);
    assert(MovieAv_Ready(m));MovieAv_Begin(m);
    uint8_t pixels[320*180*4];int32_t mix[441*2];
    for(int i=0;i<600;i++)
    {
        memset(mix,0,sizeof mix);MovieAv_Mix(m,mix,441,65536);
        int64_t clock=MovieAv_Clock(m,i*10000LL,1);
        frames+=MovieAv_Frame(m,clock,pixels);
        for(int j=0;j<882;j++)energy+=abs(mix[j]);
        if(MovieAv_Done(m,clock))break;
        Wait(10);
    }
    if(MovieAv_Error(m,error,sizeof error)){fprintf(stderr,"%s\n",error);abort();}
    assert(frames>30&&energy>1000000);
    printf("Real movie probe: %dx%d, duration=%dms, audio=%d, frames=%d, audio energy=%lld\n",info.width,info.height,info.durationMs,info.audio,frames,(long long)energy);
    MovieAv_Close(m);
}
int main(int argc,char** argv)
{
    if(argc==2){Probe(argv[1]);return 0;}
    assert(argc==6);MovieAvInfo_t info;char error[256];
    assert(!MovieAv_Open("/missing-movie",1,&info,error,sizeof error)&&error[0]);
    assert(!MovieAv_Open(argv[5],1,&info,error,sizeof error));
    Play(argv[1],1,65536,0,0);Play(argv[1],1,0,0,0);
    Play(argv[2],1,65536,0,0);Play(argv[3],1,65536,0,0);
    Play(argv[4],0,65536,0,1);Play(argv[1],0,65536,0,0);Play(argv[1],1,65536,1,0);
    for(int i=0;i<20;i++)
    {
        MovieAv_t* m=Open(argv[1],1,&info);Wait(5);
        int64_t start=Now();MovieAv_Close(m);assert(Now()-start<1000000);
    }
    puts("Movie decoder playback, pause, mute, resampling, VFR, fallback and blocked-stop checks passed");return 0;
}
