/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/os_posix.h"
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(v) do { if(!(v)) { fprintf(stderr,"posixpaths:%d: %s\n",__LINE__,#v); exit(1); } } while(0)
char gPosixExeDir[PATH_MAX];
// These tests isolate path matching; encoding conversion has its own tests.
void OsPosix_SjisToUtf8(const char* in,char* out,size_t n){snprintf(out,n,"%s",in);}
static void Touch(const char* path){FILE* f=fopen(path,"wb");CHECK(f);CHECK(!fclose(f));}
static void Expect(const char* in,const char* expected)
{
    char path[PATH_MAX];OsPosix_Path(in,path,sizeof path);
    if(strcmp(path,expected)){fprintf(stderr,"%s -> %s, expected %s\n",in,path,expected);exit(1);}
}
static void LongName(char* out,unsigned i){snprintf(out,512,"Huge/%0190u_%04u.CBG",0u,i);}
static void* Reader(void* ignored)
{
    (void)ignored;
    for(unsigned n=0;n<500;++n){Expect("archive\\portrait.cbg","Archive/Portrait.CBG");Expect("archive\\not-present","Archive/not-present");}
    return NULL;
}
int main(void)
{
    char root[PATH_MAX],cwd[PATH_MAX],path[512],query[512];
    const char* temp=getenv("TMPDIR");
    CHECK(snprintf(root,sizeof root,"%s/openbgi-paths-XXXXXX",temp?temp:"/tmp")<(int)sizeof root);
    CHECK(getcwd(cwd,sizeof cwd));CHECK(mkdtemp(root));CHECK(!chdir(root));
    snprintf(gPosixExeDir,sizeof gPosixExeDir,"%s",root);
    CHECK(!mkdir("Archive",0700));Touch("Archive/Portrait.CBG");Touch("Root.TXT");
    CHECK(!mkdir("Alternate",0700));Touch("Alternate/Other.TXT");
    for(unsigned i=0;i<6;++i){snprintf(path,sizeof path,"Dir%u",i);CHECK(!mkdir(path,0700));snprintf(path,sizeof path,"Dir%u/Slot.CBG",i);Touch(path);}
    CHECK(!mkdir("Huge",0700));
    for(unsigned i=0;i<1600;++i){LongName(path,i);Touch(path);}
    sleep(3); // permit stable snapshots, including filesystems with coarse timestamps

    Expect("archive\\portrait.cbg","Archive/Portrait.CBG");
    Expect("archive\\not-present","Archive/not-present");
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i)CHECK(!pthread_create(&threads[i],NULL,Reader,NULL));
    for(unsigned i=0;i<4;++i)CHECK(!pthread_join(threads[i],NULL));

    CHECK(!rename("Archive/Portrait.CBG","Archive/Portrait.New"));
    Expect("archive\\portrait.cbg","Archive/portrait.cbg");
    Expect("archive\\portrait.new","Archive/Portrait.New");
    Touch("Archive/New.Cbg");Expect("archive\\new.cbg","Archive/New.Cbg");
    CHECK(!rename("Archive/New.Cbg","Archive/NEW.CBG"));Expect("archive\\new.cbg","Archive/NEW.CBG");
    CHECK(!unlink("Archive/NEW.CBG"));Expect("archive\\new.cbg","Archive/new.cbg");
    CHECK(!rename("Archive","archive"));Expect("ARCHIVE\\portrait.new","archive/Portrait.New");

    // Relative cache keys must not survive a change of working directory.
    Expect("root.txt","Root.TXT");CHECK(!chdir("Alternate"));Expect("other.txt","Other.TXT");
    CHECK(!chdir(".."));Expect("root.txt","Root.TXT");
    for(unsigned round=0;round<3;++round)for(unsigned i=0;i<6;++i){
        snprintf(query,sizeof query,"Dir%u/slot.cbg",i);snprintf(path,sizeof path,"Dir%u/Slot.CBG",i);Expect(query,path);
    }
    // A directory larger than the snapshot budget must fall back without
    // publishing a partial list or hiding names beyond the budget.
    for(unsigned i=0;i<1600;i+=1599){
        LongName(path,i);snprintf(query,sizeof query,"%s",path);strcpy(query+strlen(query)-3,"cbg");Expect(query,path);
    }

    CHECK(!unlink("archive/Portrait.New"));CHECK(!rmdir("archive"));CHECK(!unlink("Root.TXT"));
    CHECK(!unlink("Alternate/Other.TXT"));CHECK(!rmdir("Alternate"));
    for(unsigned i=0;i<6;++i){snprintf(path,sizeof path,"Dir%u/Slot.CBG",i);CHECK(!unlink(path));snprintf(path,sizeof path,"Dir%u",i);CHECK(!rmdir(path));}
    for(unsigned i=0;i<1600;++i){LongName(path,i);CHECK(!unlink(path));}CHECK(!rmdir("Huge"));
    CHECK(!chdir(cwd));CHECK(!rmdir(root));
    puts("PASS posixpaths: case matching, misses, immediate create/rename/delete, changed cwd, eviction, oversize fallback and concurrent readers");
    return 0;
}
