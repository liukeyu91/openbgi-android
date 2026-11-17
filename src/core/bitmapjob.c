/* SPDX-License-Identifier: GPL-2.0-only
 * Background image reads and CBG decoding. Only immutable codec input,
 * private file/output buffers and the locked CBG cache reach the worker.
 * Archive lookup, VM waits, file-cache updates and bitmap creation stay
 * on the VM thread. Unsupported formats retain the existing loader path.
 */
#include "bgi/file.h"
#include "bgi/codec.h"
#include "bgi/version.h"
#ifdef BGI_ANDROID
#include "bgi/crash.h"
#endif

struct BitmapJob {
    FileView_t view;
    OsThread_t* worker;
    uint8_t *raw,*output;
    uint32_t bytes,done;
    uint32_t readMs,decodeMs;
    uint16_t width,height;
    char name[0x104];
    int reported;
    int legacy;
};

static void Decode(void* context)
{
    BitmapJob_t* job=context;
    uint32_t start=OS_TicksMs();
#ifdef BGI_ANDROID
    OsAndroid_CrashThread();
#endif
    uint32_t read=File_Read(&job->view.file,job->raw,job->view.size);
    job->readMs=OS_TicksMs()-start;start=OS_TicksMs();
    if(read!=job->view.size)
        job->bytes=0xffffffffu;
    else if(BGI_CbgDecodeCached(job->raw,job->view.size,job->output)!=0) {
        if(job->legacy) {
            memcpy(job->output,job->raw,job->view.size);
            job->bytes=job->view.size; // legacy LoadFile leaves bad CBG stored
        } else job->bytes=0xffffffffu;
    }
    job->decodeMs=OS_TicksMs()-start;
    File_Close(&job->view.file);
    __atomic_store_n(&job->done,1,__ATOMIC_RELEASE);
}

BitmapJob_t* Loader_StartBitmap(uint8_t** dst, const char* arc, const char* name)
{
    FileView_t view;
    CbgHeader_t header;
    BitmapJob_t* job;
    uint64_t decoded,capacity;
    uint32_t bpp,storedLimit;
    int legacy=gEngine->gen<GEN_1_69_472;
    if(!Loader_IsRunning() || !LoadFile_OpenView(&view,arc,name))return NULL;
    if(view.size<sizeof header || File_Read(&view.file,&header,sizeof header)!=sizeof header ||
        !BGI_IsCbg(&header))goto fallback;
    bpp=header.bpp&0xffffu;
    if(bpp!=8 && bpp!=24 && bpp!=32)goto fallback;
    decoded=16u+(uint64_t)header.width*header.height*(!legacy && bpp==24?4:bpp/8);
    /* Small buttons stay inline: avoid starting a worker for cheap loads. */
    if(!header.width || !header.height || decoded<1024*1024 || decoded>UINT32_MAX-0x30u)goto fallback;
    capacity=decoded>view.size?decoded:view.size;
    storedLimit=gEngine->gen<=GEN_1_69_472?40u*1024*1024:
        gEngine->gen==GEN_1_494?48u*1024*1024:64u*1024*1024;
    if(!legacy && view.size>storedLimit)goto fallback;
    if(!File_Seek(&view.file,view.offset))goto fallback;
    job=BGI_Calloc(sizeof *job);job->view=view;job->legacy=legacy;
    job->width=header.width;job->height=header.height;
    snprintf(job->name,sizeof job->name,"%s",name);
    job->bytes=(uint32_t)decoded;
    job->raw=BGI_Alloc((size_t)view.size+0x30);
    memset(job->raw+view.size,0,0x30);
    job->output=BGI_Alloc((size_t)capacity+0x30);
    memset(job->output+capacity,0,0x30);
    *dst=job->output;
    job->worker=OS_ThreadStart(Decode,job,OS_PRIO_NORMAL);
    if(!job->worker)Decode(job); // thread creation failure keeps loading functional
    return job;
fallback:
    File_Close(&view.file);
    return NULL;
}

int Loader_PollBitmap(BitmapJob_t* job, uint32_t* size)
{
    if(!__atomic_load_n(&job->done,__ATOMIC_ACQUIRE))return 0;
    if(!job->reported) {
        job->reported=1;
        fprintf(stderr,"bgi: background bitmap %s: %ux%u, read %u ms, decode/cache %u ms\n",
            job->name,(unsigned)job->width,(unsigned)job->height,job->readMs,job->decodeMs);
    }
    *size=job->bytes;
    return 1;
}

void Loader_DestroyBitmap(BitmapJob_t* job)
{
    if(!job)return;
    OS_ThreadJoin(job->worker);
    File_Close(&job->view.file);
    BGI_Free(job->raw);
    BGI_Free(job); // output belongs to the wait object, released after the join
}
