/* SPDX-License-Identifier: GPL-2.0-only
 * Exercise the actual Android startup routing on the host. Only AndroidHost,
 * logging and the OS chdir adapter are replaced; storage.c is unchanged. */
#define BGI_ANDROID_INTERNAL_H
#define BGI_OS_POSIX_INTERNAL_H
#include "bgi/version.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {char internal[PATH_MAX],external[PATH_MAX];} AndroidHost_t;
static AndroidHost_t* gAndroidHost;
static char gPosixConfigDir[PATH_MAX],gPosixRegFile[PATH_MAX],chosen[PATH_MAX];
static int OS_SetGameDir(const char* path){snprintf(chosen,sizeof chosen,"%s",path);return access(path,F_OK)==0;}
static void OsAndroid_Log(const char* message){(void)message;}
int Engine_ProfileCount(void){return 0;}
const EngineProfile_t* Engine_ProfileAt(int i){(void)i;return NULL;}
#include "../../src/os/android/storage.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"androidstorage:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put(const char* name,const char* value){FILE* f=fopen(name,"w");CHECK(f!=NULL);CHECK(fputs(value,f)>=0);CHECK(fclose(f)==0);}
int main(void){
    char root[]="/tmp/openbgi-storage-XXXXXX",path[PATH_MAX],reference[PATH_MAX],options[128];AndroidHost_t host;
    CHECK(mkdtemp(root)!=NULL);snprintf(host.internal,sizeof host.internal,"%s/internal",root);snprintf(host.external,sizeof host.external,"%s/external",root);
    CHECK(mkdir(host.internal,0700)==0 && mkdir(host.external,0700)==0);gAndroidHost=&host;
    snprintf(path,sizeof path,"%s/games",host.internal);CHECK(mkdir(path,0700)==0);
    snprintf(path,sizeof path,"%s/games/first",host.internal);CHECK(mkdir(path,0700)==0);
    snprintf(path,sizeof path,"%s/active-game.txt",host.internal);put(path,"first");
    CHECK(OsAndroid_InitPaths());snprintf(path,sizeof path,"%s/games/first",host.internal);CHECK(!strcmp(chosen,path));
    char source[PATH_MAX];snprintf(source,sizeof source,"%s/original Game",root);CHECK(mkdir(source,0700)==0);
    snprintf(reference,sizeof reference,"%s/source-path.txt",gPosixConfigDir);put(reference,source);
    CHECK(OsAndroid_InitPaths()&&!strcmp(chosen,source));
    snprintf(path,sizeof path,"%s/games/first/.openbgi",host.internal);CHECK(!strcmp(gPosixConfigDir,path));
    snprintf(path,sizeof path,"%s/settings.ini",gPosixConfigDir);put(path,"encoding=gbk\nprofile=auto\n");OsAndroid_EngineOptions(options,sizeof options);
    CHECK(!strcmp(options,"--text-encoding=gbk"));CHECK(unlink(path)==0);
    put(reference,"relative");CHECK(!OsAndroid_InitPaths());put(reference,"");CHECK(!OsAndroid_InitPaths());
    put(reference,"/missing/path");CHECK(!OsAndroid_InitPaths());put(reference,"/path\nother");CHECK(!OsAndroid_InitPaths());
    CHECK(unlink(reference)==0);CHECK(OsAndroid_InitPaths());CHECK(rmdir(gPosixConfigDir)==0);
    snprintf(path,sizeof path,"%s/games/first",host.internal);CHECK(rmdir(path)==0);
    snprintf(path,sizeof path,"%s/games/external",host.internal);CHECK(mkdir(path,0700)==0);
    snprintf(path,sizeof path,"%s/game",host.external);CHECK(mkdir(path,0700)==0);
    snprintf(path,sizeof path,"%s/active-game.txt",host.internal);put(path,"external");
    CHECK(OsAndroid_InitPaths());snprintf(path,sizeof path,"%s/game",host.external);CHECK(!strcmp(chosen,path));
    CHECK(rmdir(path)==0 && rmdir(gPosixConfigDir)==0);
    snprintf(path,sizeof path,"%s/games/external",host.internal);CHECK(rmdir(path)==0);
    snprintf(path,sizeof path,"%s/games",host.internal);CHECK(rmdir(path)==0);
    snprintf(path,sizeof path,"%s/active-game.txt",host.internal);CHECK(unlink(path)==0);
    CHECK(rmdir(host.internal)==0 && rmdir(host.external)==0 && rmdir(source)==0 && rmdir(root)==0);
    puts("ANDROID_STORAGE_PASS: actual native startup uses reference, preserves private config, handles missing/invalid paths and legacy imports");return 0;
}
