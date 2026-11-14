/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/gfx/bitmap.h"

#define CHECK(v) do { if(!(v)) { fprintf(stderr,"blitfast:%d case %u: %s\n",__LINE__,test,#v); exit(1); } } while(0)
static unsigned test;
static uint32_t seed=251943;
static uint32_t Rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static int Floor(int value,unsigned shift){int divisor=1<<shift;return value>=0?value/divisor:-((-value+divisor-1)/divisor);}
static unsigned Byte(uint32_t value,unsigned shift){return (value>>shift)&255u;}
static uint32_t Clamp(int value){return (uint32_t)(value<0?0:value>255?255:value);}

static uint32_t Plain(uint32_t d,uint32_t s,int level)
{
    if(level<=0)return s;
    if(level>=256)return d;
    uint32_t out=s&0xff000000u;
    for(unsigned c=0;c<24;c+=8)out|=Clamp((int)Byte(s,c)+Floor(((int)Byte(d,c)-(int)Byte(s,c))*(level/2),7))<<c;
    return out;
}
static uint32_t Cross(uint32_t d,uint32_t a,uint32_t b,int ratio,int level)
{
    if(level<0 || level>=256)return d;
    int va=(int)Byte(a,24)*(256-level)/256,vb=(int)Byte(b,24)*(256-level)/256;
    int alpha=va+Floor((vb-va)*ratio,8);
    if(!alpha)return d;
    uint32_t out=0;
    for(unsigned c=0;c<24;c+=8){
        int ca=(int)Byte(a,c)*va/256,cb=(int)Byte(b,c)*vb/256;
        int colour=ca+Floor((cb-ca)*ratio,8)+Floor((int)Byte(d,c)*(256-alpha),8);
        out|=Clamp(colour)<<c;
    }
    return out;
}
static uint32_t Mix(uint32_t a,uint32_t b,unsigned ratio,int mode)
{
    uint32_t out=0;
    if(mode==PM_RGB32){
        for(unsigned c=0;c<32;c+=8)out|=((Byte(a,c)*(256-ratio)+Byte(b,c)*ratio)/256)<<c;
    }else{
        unsigned wa=Byte(a,24)*(256-ratio),wb=Byte(b,24)*ratio,sum=(wa+wb)&65535u;
        if(!sum)return 0;
        unsigned weight=(wa*128)/sum;out=(sum/256)<<24;
        for(unsigned c=0;c<24;c+=8)out|=Clamp((int)Byte(b,c)+Floor(((int)Byte(a,c)-(int)Byte(b,c))*(int)weight,7))<<c;
    }
    return out;
}
static Bmp_t Pixel(uint32_t* pixel,int mode){Bmp_t b={0};b.w=b.h=1;b.pitch=b.bpp=4;b.pixels=(uint8_t*)pixel;b.mode=mode;return b;}

static void Pixels(void)
{
    const unsigned alphas[]={0,1,2,127,128,254,255};
    for(test=0;test<300000;++test){
        uint32_t a=Rnd(),b=Rnd(),d=Rnd(),actual=d;
        int ratio=(int)(Rnd()%257),level=(int)(Rnd()%261)-2;
        if(test%3==0)a=(a&0xffffffu)|(alphas[(test/3)%7]<<24);
        if(test%5==0)b=(b&0xffffffu)|(alphas[(test/5)%7]<<24);
        if(test%19==0)ratio=-7;
        if(test%23==0)ratio=263;
        Bmp_t source=Pixel(&a,PM_RGB32),dest=Pixel(&actual,PM_RGB32);
        Blit_Over(&dest,&source,level);CHECK(actual==Plain(d,a,level));
        actual=d;Blit_Blend(&dest,&source,level);CHECK(actual==Plain(d,a,level));
        source.mode=PM_ARGB32;Bmp_t second=Pixel(&b,PM_ARGB32);
        actual=d;CHECK(Blit_Crossfade(&dest,&source,&second,ratio,level)==0);
        CHECK(actual==Cross(d,a,b,ratio,level));
        unsigned normal=ratio<0 || ratio>256?Rnd()%257:(unsigned)ratio;
        for(int mode=PM_RGB32;mode<=PM_ARGB32;++mode){
            source.mode=second.mode=dest.mode=mode;actual=d;
            CHECK(Blit_CrossfadeTo(&dest,&source,&second,(int)normal)==0);
            CHECK(actual==Mix(a,b,normal,mode));
        }
    }
}

static void Buffers(void)
{
    enum { W=13,H=7,PITCH=16,COUNT=PITCH*H };
    for(test=0;test<4000;++test){
        uint32_t a[COUNT],b[COUNT],d[COUNT],ra[COUNT],rb[COUNT],rd[COUNT];
        for(unsigned i=0;i<COUNT;++i){
            a[i]=Rnd();b[i]=Rnd();d[i]=Rnd();
            if(test%7==0){a[i]&=0xffffffu;b[i]&=0xffffffu;}
            if(test%11==0){a[i]|=0xff000000u;b[i]|=0xff000000u;}
        }
        memcpy(ra,a,sizeof a);memcpy(rb,b,sizeof b);memcpy(rd,d,sizeof d);
        unsigned ratio=Rnd()%257;int level=(int)(Rnd()%257),alias=(int)(test%3),to=(int)((test/3)%2);
        int mode=to && test%2?PM_RGB32:PM_ARGB32;
        unsigned offset=test%4;
        Bmp_t aa=Pixel(a+offset,mode),bb=Pixel(b+offset,mode),dd=Pixel((alias==0?d:alias==1?a:b)+offset,to?mode:PM_RGB32);
        aa.w=bb.w=dd.w=W;aa.h=bb.h=dd.h=H;aa.pitch=bb.pitch=dd.pitch=PITCH*4;
        uint32_t* expected=alias==0?rd:alias==1?ra:rb;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){unsigned i=offset+y*PITCH+x;expected[i]=to?Mix(ra[i],rb[i],ratio,mode):Cross(expected[i],ra[i],rb[i],(int)ratio,level);}
        if(to)CHECK(Blit_CrossfadeTo(&dd,&aa,&bb,(int)ratio)==0);
        else CHECK(Blit_Crossfade(&dd,&aa,&bb,(int)ratio,level)==0);
        CHECK(!memcmp(a,ra,sizeof a) && !memcmp(b,rb,sizeof b) && !memcmp(d,rd,sizeof d));
    }
}

static void AlphaBuffers(void)
{
    enum { W=13,H=7,PITCH=16,COUNT=PITCH*H };
    for(test=0;test<4000;++test){
        uint32_t s[COUNT],d[COUNT],rs[COUNT],rd[COUNT];
        unsigned offset=test%4,alias=test%2;int level=(int)(Rnd()%259)-1;
        for(unsigned i=0;i<COUNT;++i){
            s[i]=Rnd();d[i]=Rnd();
            if(test%7==0)s[i]&=0xffffffu;
            if(test%11==0)s[i]|=0xff000000u;
        }
        memcpy(rs,s,sizeof s);memcpy(rd,d,sizeof d);
        uint32_t* expected=alias?rs:rd;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned i=offset+y*PITCH+x,weight=level<=0?Byte(rs[i],24)/2:(Byte(rs[i],24)/2)*(unsigned)(256-level)/256;
            if(level>=256)continue;
            uint32_t out=expected[i]&0xff000000u;
            for(unsigned c=0;c<24;c+=8)out|=((Byte(rs[i],c)*weight+Byte(expected[i],c)*(128-weight))/128)<<c;
            expected[i]=out;
        }
        Bmp_t source=Pixel(s+offset,PM_ARGB32),dest=Pixel((alias?s:d)+offset,PM_RGB32);
        source.w=dest.w=W;source.h=dest.h=H;source.pitch=dest.pitch=PITCH*4;
        Blit_Blend(&dest,&source,level);
        CHECK(!memcmp(s,rs,sizeof s) && !memcmp(d,rd,sizeof d));
    }
}

static void PartialOverlapBuffers(void)
{
    enum { W=17,H=3,PITCH=25,COUNT=PITCH*H+16 };
    for(test=0;test<3000;++test){
        uint32_t a[COUNT],b[COUNT],ra[COUNT],rb[COUNT];
        for(unsigned i=0;i<COUNT;++i){a[i]=Rnd();b[i]=Rnd();}
        memcpy(ra,a,sizeof a);memcpy(rb,b,sizeof b);
        int delta=(int)(test%7)-3,alias=(int)((test/7)%2),alpha=(int)((test/14)%2);
        int level=(int)(Rnd()%257),ratio=(int)(Rnd()%257);
        uint32_t* expected=alias?rb:ra;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned si=4+y*PITCH+x,di=(unsigned)(4+delta)+y*PITCH+x;
            if(alpha){
                unsigned weight=level<=0?Byte(ra[si],24)/2:(Byte(ra[si],24)/2)*(unsigned)(256-level)/256;
                if(level>=256)continue;
                uint32_t out=expected[di]&0xff000000u;
                for(unsigned c=0;c<24;c+=8)out|=((Byte(ra[si],c)*weight+Byte(expected[di],c)*(128-weight))/128)<<c;
                expected[di]=out;
            }else expected[di]=Cross(expected[di],ra[si],rb[si],ratio,level);
        }
        Bmp_t aa=Pixel(a+4,PM_ARGB32),bb=Pixel(b+4,PM_ARGB32),dd=Pixel((alias?b:a)+4+delta,PM_RGB32);
        aa.w=bb.w=dd.w=W;aa.h=bb.h=dd.h=H;aa.pitch=bb.pitch=dd.pitch=PITCH*4;
        if(alpha)Blit_Blend(&dd,&aa,level);
        else CHECK(Blit_Crossfade(&dd,&aa,&bb,ratio,level)==0);
        CHECK(!memcmp(a,ra,sizeof a) && !memcmp(b,rb,sizeof b));
    }
}

int main(void)
{
    Pixels();Buffers();AlphaBuffers();PartialOverlapBuffers();
    puts("PASS blitfast: 300000 pixels, 4000 cross-fade, 4000 alpha and 3000 partially overlapping buffers");
    return 0;
}
