/* SPDX-License-Identifier: GPL-2.0-only
 * Hold the real decoder at a barrier to verify that image waits yield,
 * then compare every pixel. Files, decoder and OS threads are production
 * code; only the timing barrier and thread-creation failure are injected.
 */
#include "bgi/file.h"
#include "bgi/codec.h"
#include "bgi/waitobj.h"
#include "bgi/wait.h"
#include "bgi/gfx/bmpmgr.h"
#include "bgi/version.h"
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"bitmapjob:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned held,entered,released,finished,calls,failThread;
uint32_t __real_BGI_CbgDecode(const void*,void*);
OsThread_t* __real_OS_ThreadStart(OsThreadFn_t,void*,int);
uint32_t __wrap_BGI_CbgDecode(const void* src,void* dst)
{
    __atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);
    __atomic_add_fetch(&entered,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&held,__ATOMIC_ACQUIRE) && !__atomic_load_n(&released,__ATOMIC_ACQUIRE))OS_SleepMs(1);
    uint32_t result=__real_BGI_CbgDecode(src,dst);
    __atomic_add_fetch(&finished,1,__ATOMIC_RELEASE);
    return result;
}
OsThread_t* __wrap_OS_ThreadStart(OsThreadFn_t fn,void* arg,int priority)
{
    return failThread?NULL:__real_OS_ThreadStart(fn,arg,priority);
}
static void Hold(void)
{
    held=1;entered=released=finished=calls=0;
}
static void WaitEntered(unsigned count)
{
    uint32_t start=OS_TicksMs();
    while(__atomic_load_n(&entered,__ATOMIC_ACQUIRE)<count && OS_TicksMs()-start<5000)OS_SleepMs(1);
    CHECK(__atomic_load_n(&entered,__ATOMIC_ACQUIRE)==count);
}
static void Release(void){__atomic_store_n(&released,1,__ATOMIC_RELEASE);}
static uint32_t Complete(BitmapJob_t* job)
{
    uint32_t size=0,start=OS_TicksMs();
    while(!Loader_PollBitmap(job,&size) && OS_TicksMs()-start<5000)OS_SleepMs(1);
    CHECK(Loader_PollBitmap(job,&size));return size;
}
static void Save(const char* name,const uint8_t* data,uint32_t size)
{
    FILE* f=fopen(name,"wb");CHECK(f!=NULL);CHECK(fwrite(data,1,size,f)==size);CHECK(fclose(f)==0);
}
static void LaterRelease(void* unused){BGI_UNUSED(unused);OS_SleepMs(20);Release();}
int main(void)
{
    char directory[128];snprintf(directory,sizeof directory,"/tmp/bgi_bitmapjob_%ld",(long)getpid());
    CHECK(mkdir(directory,0700)==0 && chdir(directory)==0);OS_Init();
    Engine_ResetGames();CHECK(Engine_SelectProfile("1.659.8"));SetBaseDir(NULL);
    gArcMgr=FileSet_New();gBmpMgr=BmpMgr_New(1);Loader_Start();BGI_CbgCacheSetLimit(8*1024*1024);
    const uint32_t bytes=16+512*512*4;
    uint8_t* raw=calloc(1,bytes);CHECK(raw!=NULL);
    RawImageHeader_t* header=(RawImageHeader_t*)raw;header->width=header->height=512;header->bpp=32;
    for(uint32_t i=16;i<bytes;i++)raw[i]=(uint8_t)(i*17+i/83);
    uint32_t stored;uint8_t* compressed=BGI_CbgEncode(raw,73,&stored);Save("image.cbg",compressed,stored);

    /* A pending decoder must not block VM polling or a cache clear. */
    Thread_t* thread=Thread_New(64,256,256);Loader_SetCacheSize(4*1024*1024);Hold();
    Wait_t* wait=WaitBmpLoad_New(thread,0,"0","image.cbg");WaitEntered(1);
    for(int tick=0;tick<10;tick++)CHECK(wait->vt->poll(wait)==0);
    BGI_CbgCacheClear();Loader_FreeCache();Release();
    uint32_t start=OS_TicksMs();int status;
    while((status=wait->vt->poll(wait))==0 && OS_TicksMs()-start<5000)OS_SleepMs(1);
    CHECK(status==1);Bmp_t bitmap;CHECK(BmpMgr_GetInfo(gBmpMgr,&bitmap,0));
    CHECK(!memcmp(bitmap.pixels,raw+16,bytes-16));wait->vt->destroy(wait);
    /* Clearing while decoding prevents the in-flight job repopulating cache. */
    uint8_t* direct=malloc(bytes);CHECK(direct!=NULL);
    CHECK(BGI_CbgDecodeCached(compressed,stored,direct)==0 && calls==2);
    CHECK(!memcmp(direct,raw,bytes));free(direct);BGI_CbgCacheClear();

    /* Two independently owned handles/decodes can finish together. */
    Hold();uint8_t *a=NULL,*b=NULL;
    BitmapJob_t* first=Loader_StartBitmap(&a,NULL,"image.cbg");
    BitmapJob_t* second=Loader_StartBitmap(&b,NULL,"image.cbg");CHECK(first && second);WaitEntered(2);
    Release();CHECK(Complete(first)==bytes && Complete(second)==bytes);
    CHECK(!memcmp(a,raw,bytes) && !memcmp(b,raw,bytes));
    Loader_DestroyBitmap(first);Loader_DestroyBitmap(second);free(a);free(b);

    /* A cache hit performs no second decode, but still verifies source bytes. */
    held=0;unsigned before=calls;
    first=Loader_StartBitmap(&a,NULL,"image.cbg");CHECK(first && Complete(first)==bytes);
    CHECK(calls==before && !memcmp(a,raw,bytes));Loader_DestroyBitmap(first);free(a);

    /* Real archive offset plus pseudo-archive resolution. */
    FILE* arc=fopen("images.arc","wb");CHECK(arc!=NULL);
    uint32_t count=1;PackEntry_t entry;memset(&entry,0,sizeof entry);
    memcpy(entry.name,"image.cbg",10);entry.offset=7;entry.size=stored;
    CHECK(fwrite(PACKFILE_MAGIC,1,12,arc)==12 && fwrite(&count,4,1,arc)==1);
    CHECK(fwrite(&entry,sizeof entry,1,arc)==1);CHECK(fwrite("padding",1,7,arc)==7);
    CHECK(fwrite(compressed,1,stored,arc)==stored);fclose(arc);
    CHECK(unlink("image.cbg")==0);
    FileSet_t* list=FileSetList_New();char* names[]={"images.arc",NULL};
    CHECK(FileSetList_SetName(list,"list.arc") && FileSetList_SetList(list,names));CHECK(ArcMgr_Register(gArcMgr,list));
    for(int pseudo=0;pseudo<2;pseudo++) {
        first=Loader_StartBitmap(&a,pseudo?"list.arc":"images.arc","image.cbg");
        CHECK(first && Complete(first)==bytes && !memcmp(a,raw,bytes));Loader_DestroyBitmap(first);free(a);
    }

    /* Destroying a wait while it is pending joins before its buffer is freed. */
    BGI_CbgCacheClear();Hold();wait=WaitBmpLoad_New(thread,0,"images.arc","image.cbg");WaitEntered(1);
    OsThread_t* releaser=OS_ThreadStart(LaterRelease,NULL,OS_PRIO_NORMAL);CHECK(releaser!=NULL);
    wait->vt->destroy(wait);CHECK(__atomic_load_n(&finished,__ATOMIC_ACQUIRE)==1);OS_ThreadJoin(releaser);

    /* Failure to start a worker falls back to the same decoder inline. */
    BGI_CbgCacheClear();held=0;failThread=1;
    first=Loader_StartBitmap(&a,"images.arc","image.cbg");CHECK(first && Complete(first)==bytes);
    CHECK(!memcmp(a,raw,bytes));Loader_DestroyBitmap(first);free(a);failThread=0;

    /* 24-bit files keep packed pixels, but newer file loaders report a
     * widened byte count. Exercise both contracts, cold and cached. */
    uint32_t rgbBytes=16+512*700*3;
    uint8_t* rgb=calloc(1,rgbBytes);CHECK(rgb!=NULL);
    RawImageHeader_t* rgbHeader=(RawImageHeader_t*)rgb;
    rgbHeader->width=512;rgbHeader->height=700;rgbHeader->bpp=24;
    for(uint32_t i=16;i<rgbBytes;i++)rgb[i]=(uint8_t)(i*11);
    uint32_t rgbStored;uint8_t* rgbCompressed=BGI_CbgEncode(rgb,93,&rgbStored);
    Save("rgb.cbg",rgbCompressed,rgbStored);BGI_CbgCacheClear();
    for(int legacy=0;legacy<2;legacy++) {
        CHECK(Engine_SelectProfile(legacy?"1.69/444":"1.659.8"));
        first=Loader_StartBitmap(&a,NULL,"rgb.cbg");
        CHECK(first && Complete(first)==(legacy?rgbBytes:16+512*700*4));
        CHECK(!memcmp(a,rgb,rgbBytes));Loader_DestroyBitmap(first);free(a);
    }
    CHECK(unlink("rgb.cbg")==0);free(rgbCompressed);free(rgb);
    CHECK(Engine_SelectProfile("1.659.8"));

    /* Single-channel masks also use the worker above the same threshold. */
    uint32_t grayBytes=16+1024*1024;
    uint8_t* gray=calloc(1,grayBytes);CHECK(gray!=NULL);
    RawImageHeader_t* grayHeader=(RawImageHeader_t*)gray;
    grayHeader->width=grayHeader->height=1024;grayHeader->bpp=8;
    for(uint32_t i=16;i<grayBytes;i++)gray[i]=(uint8_t)(i*19);
    uint32_t grayStored;uint8_t* grayCompressed=BGI_CbgEncode(gray,101,&grayStored);
    Save("gray.cbg",grayCompressed,grayStored);
    first=Loader_StartBitmap(&a,NULL,"gray.cbg");
    CHECK(first && Complete(first)==grayBytes && !memcmp(a,gray,grayBytes));
    Loader_DestroyBitmap(first);free(a);
    CHECK(unlink("gray.cbg")==0);free(grayCompressed);free(gray);

    CHECK(unlink("images.arc")==0);free(compressed);free(raw);Thread_Delete(thread);
    Loader_Stop();BmpMgr_Delete(gBmpMgr);gBmpMgr=NULL;FileSet_Delete(gArcMgr);gArcMgr=NULL;
    OS_Shutdown();CHECK(chdir("/")==0 && rmdir(directory)==0);
    puts("PASS background bitmaps: yielding, 8/24/32-bit exact pixels, legacy sizes, concurrent jobs, cache clear/reuse, archive/list views, pending destruction and thread fallback");
    return 0;
}
