/* SPDX-License-Identifier: GPL-2.0-only
 * Deliberately crashes a disposable subprocess, never a game/device. */
#include "bgi/crash.h"
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile uintptr_t badAddress=1;
__attribute__((noinline)) static void FaultLeaf(void)
{
    *(volatile unsigned*)badAddress=7;
}
__attribute__((noinline)) static unsigned Overflow(unsigned depth)
{
    volatile unsigned char page[4096];
    page[depth%sizeof page]=(unsigned char)depth;
    return depth==100000 ? page[0] : Overflow(depth+1)+page[depth%sizeof page];
}
static void* Worker(void* mode)
{
    OsAndroid_CrashThread();
    if(!strcmp(mode,"overflow")) (void)Overflow(0);
    else { usleep(1000); FaultLeaf(); }
    return NULL;
}
int main(int argc,char** argv)
{
    unsigned i;
    if(argc!=3) return 2;
    OsAndroid_CrashInstall(argv[1]);
    for(i=0;i<80;i++)OsAndroid_CrashInstruction(7,0x100+i,i,0x9010,"cnfgwndtxt._bp");
    if(!strcmp(argv[2],"no-proc")) {
        for(i=3;i<64;i++) {
            char path[64],target[512];ssize_t length;
            snprintf(path,sizeof path,"/proc/self/fd/%u",i);
            length=readlink(path,target,sizeof target-1);
            if(length>0) { target[length]=0;
                if(strstr(target,"/mem") || strstr(target,"/maps"))close((int)i); }
        }
    }
    if(!strcmp(argv[2],"normal")) return 0;
    if(!strcmp(argv[2],"abort"))raise(SIGABRT);
    if(!strcmp(argv[2],"worker") || !strcmp(argv[2],"overflow") || !strcmp(argv[2],"concurrent")) {
        pthread_t worker;pthread_attr_t attr;
        pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,256*1024);
        if(pthread_create(&worker,&attr,Worker,argv[2])) return 3;
        pthread_attr_destroy(&attr);
        if(!strcmp(argv[2],"concurrent"))
            for(i=0;i<100000;i++)OsAndroid_CrashInstruction(7,0x200+i,i,0x91f0,"preview._bp");
        pthread_join(worker,NULL);
    }
    FaultLeaf();
    return 4;
}
