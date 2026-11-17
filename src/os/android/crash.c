/* SPDX-License-Identifier: GPL-2.0-only
 * Fatal-path code deliberately avoids stdio, allocation, locks, JNI and
 * unwinding libraries. Frame records are read through pread(/proc/self/mem),
 * never dereferenced: a broken frame pointer must not cause another fault.
 * /proc access is best effort; registers and VM breadcrumbs remain useful
 * when Android restricts it. This is not a replacement for a tombstone.
 */
#include "bgi/crash.h"
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#if __GCC_ATOMIC_INT_LOCK_FREE != 2
#error Crash diagnostics require lock-free unsigned int atomics
#endif
#define RING_COUNT 32
#define NAME_WORDS 16
#define MAP_LIMIT (256 * 1024)
typedef struct Breadcrumb {
    unsigned sequence, thread, ip, relative, opcode, name[NAME_WORDS];
} Breadcrumb;
static Breadcrumb gRing[RING_COUNT];
static unsigned gNext, gPublished, gReporting, gVmTid;
static int gReport = -1, gMemory = -1, gMaps = -1;
static struct timespec gSession;
static char gSessionText[4096], gImageText[512];
static const char gImageMarker[]="OpenBGI crash reporter v2";
static __thread unsigned char gAltStack[64 * 1024] __attribute__((aligned(16)));

/* Bounded writes also cope with interruption and a full filesystem. */
static void WriteBytes(const char* data, size_t length)
{
    unsigned attempts = 0;
    while(length && attempts++ < 32) {
        ssize_t n = write(gReport, data, length);
        if(n < 0 && errno == EINTR) continue;
        if(n <= 0) break;
        data += n; length -= (size_t)n;
    }
}
typedef struct Line { char text[512]; size_t size; } Line;
static void Text(Line* line, const char* text)
{
    while(*text && line->size < sizeof line->text) line->text[line->size++] = *text++;
}
static void Number(Line* line, uint64_t value, unsigned base)
{
    char reverse[32]; unsigned n = 0;
    do { reverse[n++] = "0123456789abcdef"[value % base]; value /= base; } while(value);
    while(n && line->size < sizeof line->text) line->text[line->size++] = reverse[--n];
}
static void Field(const char* name, uint64_t value, unsigned base)
{
    Line line = {{0},0}; Text(&line,name);
    if(base == 16) Text(&line,"0x");
    Number(&line,value,base); Text(&line,"\n"); WriteBytes(line.text,line.size);
}
static void Literal(const char* text)
{
    Line line = {{0},0}; Text(&line,text); WriteBytes(line.text,line.size);
}
static unsigned NativeTid(void) { return (unsigned)syscall(SYS_gettid); }

void OsAndroid_CrashThread(void)
{
    stack_t stack;
    memset(&stack,0,sizeof stack);
    stack.ss_sp = gAltStack; stack.ss_size = sizeof gAltStack;
    (void)sigaltstack(&stack,NULL);
}

/* The VM is the only writer. Every field is atomic so a fault on an audio
 * or UI thread can safely read it. Publish complete entries, skip a slot
 * being overwritten, and never follow a VM/module/heap pointer in a handler. */
void OsAndroid_CrashInstruction(uint32_t thread, uint32_t ip, uint32_t relative,
    uint32_t opcode, const char* module)
{
    unsigned i, sequence;
    Breadcrumb* entry;
    int ended = !module;
    if(__atomic_load_n(&gReporting,__ATOMIC_ACQUIRE)) return;
    sequence=++gNext;
    if(!sequence) sequence = gNext = 1;
    entry = &gRing[(sequence-1) % RING_COUNT];
    __atomic_store_n(&entry->sequence,0,__ATOMIC_SEQ_CST);
    /* If a reader sees any replacement field, its acquire fence also sees
     * this invalidation; this matters on ARM's weaker memory ordering. */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&entry->thread,thread,__ATOMIC_RELAXED);
    __atomic_store_n(&entry->ip,ip,__ATOMIC_RELAXED);
    __atomic_store_n(&entry->relative,relative,__ATOMIC_RELAXED);
    __atomic_store_n(&entry->opcode,opcode,__ATOMIC_RELAXED);
    for(i=0;i<NAME_WORDS;i++) {
        unsigned j, word=0;
        for(j=0;j<4;j++) {
            unsigned ch = ended ? 0 : (unsigned char)*module++;
            if(!ch) ended=1;
            /* Resource names only; keep the report one record per line. */
            if(ch && (ch<32 || ch>=127)) ch='?';
            word |= ch << (j*8);
        }
        __atomic_store_n(&entry->name[i],word,__ATOMIC_RELAXED);
    }
    if(!__atomic_load_n(&gVmTid,__ATOMIC_RELAXED))
        __atomic_store_n(&gVmTid,NativeTid(),__ATOMIC_RELAXED);
    __atomic_store_n(&entry->sequence,sequence,__ATOMIC_RELEASE);
    __atomic_store_n(&gPublished,sequence,__ATOMIC_RELEASE);
}
static void DumpVm(void)
{
    unsigned end=__atomic_load_n(&gPublished,__ATOMIC_ACQUIRE), count=end<RING_COUNT?end:RING_COUNT, i;
    Field("VM owner native tid: ",__atomic_load_n(&gVmTid,__ATOMIC_RELAXED),10);
    Literal("Last VM activity (not proof that the fault occurred in the VM):\n");
    for(i=0;i<count;i++) {
        unsigned sequence=end-count+i+1, j;
        Breadcrumb* entry=&gRing[(sequence-1)%RING_COUNT];
        Line line={{0},0}; char name[NAME_WORDS*4+1];
        unsigned before=__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE);
        if(before!=sequence) continue;
        Text(&line,"T"); Number(&line,__atomic_load_n(&entry->thread,__ATOMIC_RELAXED),10);
        for(j=0;j<NAME_WORDS;j++) {
            unsigned k, word=__atomic_load_n(&entry->name[j],__ATOMIC_RELAXED);
            for(k=0;k<4;k++) name[j*4+k]=(char)(word>>(k*8));
        }
        name[sizeof name-1]=0; Text(&line," "); Text(&line,name);
        Text(&line," +0x"); Number(&line,__atomic_load_n(&entry->relative,__ATOMIC_RELAXED),16);
        Text(&line," ip=0x"); Number(&line,__atomic_load_n(&entry->ip,__ATOMIC_RELAXED),16);
        Text(&line," opcode=0x"); Number(&line,__atomic_load_n(&entry->opcode,__ATOMIC_RELAXED),16);
        Text(&line,"\n");
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(__atomic_load_n(&entry->sequence,__ATOMIC_ACQUIRE)==before) WriteBytes(line.text,line.size);
    }
}
static uintptr_t Untag(uintptr_t address)
{
#if defined(__aarch64__)
    /* Keep raw registers too. Android ARM64 user-space PAC/TBI bits are not
     * part of a file offset; this candidate is for offline symbol lookup. */
    return address & UINT64_C(0x0000ffffffffffff);
#else
    return address;
#endif
}
static void DumpFrames(uintptr_t lr, uintptr_t sp, uintptr_t fp)
{
    unsigned i, frames=0;
    Literal("Native frame candidates (frame-pointer walk; may be incomplete):\n");
    if(lr) Field("lr candidate: ",Untag(lr),16);
    if(gMemory<0) { Literal("Frame memory unavailable (/proc/self/mem denied).\n"); return; }
    fp=Untag(fp); sp=Untag(sp);
    for(i=0;i<32;i++) {
        uintptr_t pair[2], next;
        if(fp<sp || fp-sp>8*1024*1024 || fp%sizeof(uintptr_t) || fp>UINTPTR_MAX-sizeof pair) break;
        if(pread(gMemory,pair,sizeof pair,(off_t)fp)!=(ssize_t)sizeof pair) break;
        Field("return candidate: ",Untag(pair[1]),16);
        frames++;
        next=Untag(pair[0]); if(next<=fp) break; fp=next;
    }
    if(!frames) Literal("No readable caller frame.\n");
}
static void RecordSignal(int number, siginfo_t* info, void* context)
{
    uintptr_t pc=0,lr=0,sp=0,fp=0;
    struct timespec now={0,0}; ucontext_t* machine=context;
    if(__atomic_exchange_n(&gReporting,1,__ATOMIC_RELAXED)) _exit(128+number);
    Literal("OpenBGI native crash report v2\n");
    /* Prepared before any crash; includes the matching APK's native SHA256. */
    { size_t n=0; while(n<sizeof gSessionText && gSessionText[n]) n++;
      WriteBytes(gSessionText,n); }
    Literal(gImageText);
    Field("Native start epoch ms: ",(uint64_t)gSession.tv_sec*1000+gSession.tv_nsec/1000000,10);
    if(clock_gettime(CLOCK_REALTIME,&now)==0)
        Field("Crash epoch ms: ",(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000,10);
    Field("PID: ",(unsigned)getpid(),10); Field("Fault native tid: ",NativeTid(),10);
    Field("Signal: ",(unsigned)number,10);
    if(info) {
        Line line={{0},0}; int code=info->si_code;
        Text(&line,"Signal code: "); if(code<0)Text(&line,"-");
        Number(&line,code<0 ? -(int64_t)code : code,10); Text(&line,"\n"); WriteBytes(line.text,line.size);
        if(number==SIGSEGV || number==SIGBUS || number==SIGILL || number==SIGFPE)
            Field("Fault address: ",(uintptr_t)info->si_addr,16);
    }
    if(machine) {
#if defined(__aarch64__)
        unsigned i; pc=machine->uc_mcontext.pc; sp=machine->uc_mcontext.sp;
        fp=machine->uc_mcontext.regs[29]; lr=machine->uc_mcontext.regs[30];
        Field("pstate: ",machine->uc_mcontext.pstate,16);
        for(i=0;i<31;i++) {
            Line line={{0},0}; Text(&line,"x"); Number(&line,i,10); Text(&line,": 0x");
            Number(&line,machine->uc_mcontext.regs[i],16); Text(&line,"\n"); WriteBytes(line.text,line.size);
        }
#elif defined(__x86_64__)
        pc=machine->uc_mcontext.gregs[REG_RIP]; sp=machine->uc_mcontext.gregs[REG_RSP];
        fp=machine->uc_mcontext.gregs[REG_RBP];
#endif
    }
    Field("pc: ",pc,16); Field("sp: ",sp,16); Field("fp: ",fp,16); Field("lr: ",lr,16);
    DumpFrames(lr,sp,fp); DumpVm();
    Literal("Memory maps (bounded to 256 KiB):\n");
    if(gMaps>=0 && lseek(gMaps,0,SEEK_SET)>=0) {
        char buffer[1024]; size_t total=0; ssize_t n;
        while(total<MAP_LIMIT && (n=read(gMaps,buffer,sizeof buffer))>0) {
            WriteBytes(buffer,(size_t)n); total+=(size_t)n;
        }
        if(total==MAP_LIMIT) Literal("[maps limit reached]\n");
    } else Literal("Memory maps unavailable.\n");
    Literal("End. Terminated with _exit(128+signal); Android REASON_EXIT_SELF is expected.\n");
    /* Do not re-raise through ART sigchain: it can reenter indefinitely.
     * Never claim that this exit produces a system native tombstone. */
    _exit(128+number);
}
void OsAndroid_CrashInstall(const char* directory)
{
    char current[PATH_MAX], previous[PATH_MAX];
    struct sigaction action;
    static const int signals[]={SIGSEGV,SIGABRT,SIGBUS,SIGILL,SIGFPE};
    size_t i;
    if(gReport>=0) return;
    snprintf(current,sizeof current,"%s/session-current.txt",directory);
    { int session=open(current,O_RDONLY|O_CLOEXEC);
      if(session>=0) { ssize_t n=read(session,gSessionText,sizeof gSessionText-1);
          if(n>0) gSessionText[n]=0; close(session); } }
    { Dl_info image;
      if(dladdr(gImageMarker,&image))
          snprintf(gImageText,sizeof gImageText,"Engine ELF load bias: 0x%llx\nEngine image: %s\n",
              (unsigned long long)(uintptr_t)image.dli_fbase,image.dli_fname?image.dli_fname:"unknown"); }
    snprintf(current,sizeof current,"%s/native-crash.txt",directory);
    snprintf(previous,sizeof previous,"%s/native-crash-previous.txt",directory);
    /* Keep the last nonempty crash across normal launches, not just one. */
    { struct stat status;
      if(stat(current,&status)==0 && status.st_size>0 && rename(current,previous)!=0) return; }
    gReport=open(current,O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);
    if(gReport<0) return;
    gMemory=open("/proc/self/mem",O_RDONLY|O_CLOEXEC);
    gMaps=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);
    clock_gettime(CLOCK_REALTIME,&gSession);
    OsAndroid_CrashThread();
    memset(&action,0,sizeof action); action.sa_sigaction=RecordSignal;
    action.sa_flags=SA_SIGINFO|SA_ONSTACK; sigfillset(&action.sa_mask);
    for(i=0;i<sizeof signals/sizeof signals[0];i++) (void)sigaction(signals[i],&action,NULL);
}
