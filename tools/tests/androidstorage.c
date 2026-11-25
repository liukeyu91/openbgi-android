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
static char overlay[PATH_MAX],data[PATH_MAX];
static int OsPosix_SetDataDirectory(const char* path){
    snprintf(data,sizeof data,"%s",path);
    return !*path || access(path,F_OK)==0;
}
static int OsPosix_SetPatchDirectory(const char* path){
    snprintf(overlay,sizeof overlay,"%s",path);
    return !*path || access(path,F_OK)==0;
}
static void OsAndroid_Log(const char* message){(void)message;}
static char mounted[PATH_MAX];
static int OsPosix_MountDisc(const char* path){
    if(!path){mounted[0]=0;return 1;}
    snprintf(mounted,sizeof mounted,"%s",path);
    return access(path,F_OK)==0;
}
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
    snprintf(path,sizeof path,"%s/data",gPosixConfigDir);CHECK(mkdir(path,0700)==0);
    CHECK(OsAndroid_InitPaths()&&!strcmp(chosen,source)&&!strcmp(data,path));
    snprintf(path,sizeof path,"%s/games/first/.openbgi",host.internal);CHECK(!strcmp(gPosixConfigDir,path));
    snprintf(path,sizeof path,"%s/settings.ini",gPosixConfigDir);put(path,"encoding=gbk\nprofile=auto\n");OsAndroid_EngineOptions(options,sizeof options);
    CHECK(!strcmp(options,"--text-encoding=gbk"));
    put(path,"encoding=big5\narchiveLanguage=tc\nprofile=auto\n");
    OsAndroid_EngineOptions(options,sizeof options);
    CHECK(!strcmp(options,"--text-encoding=big5 --archive-language=tc"));
    CHECK(unlink(path)==0);
    {
        char patches[PATH_MAX], patch[PATH_MAX], active[PATH_MAX];
        const char id[]="12345678-1234-1234-1234-123456789abc";
        snprintf(patches,sizeof patches,"%s/patches",gPosixConfigDir);
        snprintf(patch,sizeof patch,"%s/%s",patches,id);
        snprintf(active,sizeof active,"%s/active",patches);
        CHECK(mkdir(patches,0700)==0 && mkdir(patch,0700)==0);
        put(active,id);
        CHECK(OsAndroid_InitPaths() && !strcmp(overlay,patch) && !strcmp(chosen,source));
        CHECK(unlink(active)==0 && rmdir(patch)==0 && rmdir(patches)==0);
        CHECK(OsAndroid_InitPaths() && !*overlay);
    }
    put(reference,"relative");CHECK(!OsAndroid_InitPaths());put(reference,"");CHECK(!OsAndroid_InitPaths());
    put(reference,"/missing/path");CHECK(!OsAndroid_InitPaths());put(reference,"/path\nother");CHECK(!OsAndroid_InitPaths());
    CHECK(unlink(reference)==0);CHECK(OsAndroid_InitPaths() && !*data);
    /* An image entry reads through the mount; its writable layer is the private data directory. */
    {
        char entry[PATH_MAX], config[PATH_MAX], layer[PATH_MAX], image[PATH_MAX];
        snprintf(entry,sizeof entry,"%s/games/imagelib",host.internal);
        snprintf(config,sizeof config,"%s/.openbgi",entry);
        snprintf(layer,sizeof layer,"%s/data",config);
        snprintf(image,sizeof image,"%s/Game Disc.iso",root);
        CHECK(mkdir(entry,0700)==0 && mkdir(config,0700)==0 && mkdir(layer,0700)==0);
        put(image,"iso");
        snprintf(path,sizeof path,"%s/active-game.txt",host.internal);put(path,"imagelib");
        snprintf(path,sizeof path,"%s/source.txt",config);put(path,image);
        mounted[0]=0;
        CHECK(OsAndroid_InitPaths() && !strcmp(chosen,entry) && !strcmp(mounted,image) && !strcmp(data,layer));
        CHECK(!*overlay);
        /* Entries added by the first disc-image build used the older name. */
        snprintf(path,sizeof path,"%s/source.txt",config);CHECK(unlink(path)==0);
        snprintf(path,sizeof path,"%s/source-image.txt",config);put(path,image);
        CHECK(OsAndroid_InitPaths() && !strcmp(mounted,image));
        /* A relative or missing image is refused before the engine starts. */
        put(path,"relative/game.iso");CHECK(!OsAndroid_InitPaths());
        put(path,"/missing/game.iso");CHECK(!OsAndroid_InitPaths());
        put(path,"/path\nother");CHECK(!OsAndroid_InitPaths());
        put(path,image);CHECK(OsAndroid_InitPaths());
        /* Without the private layer the entry cannot run. */
        CHECK(rmdir(layer)==0 && !OsAndroid_InitPaths());
        snprintf(path,sizeof path,"%s/active-game.txt",host.internal);put(path,"first");
        snprintf(path,sizeof path,"%s/source-image.txt",config);CHECK(unlink(path)==0);
        CHECK(rmdir(config)==0 && rmdir(entry)==0 && unlink(image)==0);
        mounted[0]=0;
        CHECK(OsAndroid_InitPaths() && !*data && !mounted[0]);
    }
    snprintf(path,sizeof path,"%s/data",gPosixConfigDir);CHECK(rmdir(path)==0 && rmdir(gPosixConfigDir)==0);
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
