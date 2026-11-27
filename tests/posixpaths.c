/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/os_posix.h"
#include "bgi/bytes.h"
#include "bgi/os.h"
#include "bgi/deflate.h"
#include "../src/os/posix/disc_internal.h"
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(v) do { if(!(v)) { fprintf(stderr,"posixpaths:%d: %s\n",__LINE__,#v); exit(1); } } while(0)
char gPosixExeDir[PATH_MAX];
// These tests isolate path matching; encoding conversion has its own tests.
void OsPosix_SjisToUtf8(const char* in,char* out,size_t n){snprintf(out,n,"%s",in);}
size_t OS_SjisToUtf8(const char* in,char* out,size_t n){OsPosix_SjisToUtf8(in,out,n);return strlen(out);}
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

static void ExpectArchive(const char* logical, char byte)
{
    OsFindData_t found;
    OsFind_t* search = OS_FindFirst(logical, &found);
    CHECK(search && !OS_FindNext(search, &found));
    OS_FindClose(search);
    CHECK(OS_FileAttrs(logical) != OS_INVALID_ATTRS);
    OsFile_t* file = OS_FileOpenRead(logical);
    char actual;
    CHECK(file && OS_FileRead(file, &actual, 1) == 1 && actual == byte);
    OS_FileClose(file);
}

static void LocalizedArchives(void)
{
    static const char* const names[] = {
        "SYSTEM.C2.ARC", "System.CN.arc", "system.TC.arc", "system.en.arc",
        "archive/Data01000.CN.ARC", "archive/data01000.en.arc"
    };
    for(unsigned i = 0; i < sizeof names / sizeof names[0]; ++i)
    {
        FILE* file = fopen(names[i], "wb");
        CHECK(file && fputc('a' + i, file) != EOF && !fclose(file));
    }
    CHECK(OS_FileAttrs("system.arc") == OS_INVALID_ATTRS);
    CHECK(OsPosix_SetArchiveLanguage("cn"));
    ExpectArchive("system.arc", 'a');
    ExpectArchive("system.cn.arc", 'b');
    CHECK(OsPosix_SetArchiveLanguage("tc"));
    ExpectArchive("system.arc", 'c');
    ExpectArchive("archive\\data01000.arc", 'e');
    CHECK(OsPosix_SetArchiveLanguage("en"));
    ExpectArchive("system.arc", 'd');
    ExpectArchive("archive\\data01000.arc", 'f');

    OsFindData_t found;
    OsFind_t* search = OS_FindFirst("ARCHIVE\\data0????.arc", &found);
    CHECK(search && !strcmp(found.name, "data01000.arc") && !OS_FindNext(search, &found));
    OS_FindClose(search);
    CHECK(!unlink("system.en.arc"));
    CHECK(OS_FileAttrs("system.arc") == OS_INVALID_ATTRS);
    CHECK(!OS_FileOpenRead("system.arc") && !OS_FindFirst("system.arc", &found));
    CHECK(OsPosix_SetArchiveLanguage(""));
    for(unsigned i = 0; i < sizeof names / sizeof names[0]; ++i)
        if(strcmp(names[i], "system.en.arc")) CHECK(!unlink(names[i]));
    puts("PASS posixpaths: localized archive lookup, reads and enumeration agree");
}

static void PutByte(const char* path, int c)
{
    FILE* file = fopen(path,"wb");
    CHECK(file && fputc(c,file)==c && !fclose(file));
}

static void PatchOverlay(void)
{
    char patch[PATH_MAX], path[PATH_MAX];
    snprintf(patch,sizeof patch,"%s/Overlay",gPosixExeDir);
    CHECK(!mkdir(patch,0700));
    PutByte("SYSTEM.TC.ARC",'o');
    PutByte("Overlay/System.ARC",'p');
    PutByte("Data00010.arc",'d');
    PutByte("Overlay/DATA00020.ARC",'e');
    CHECK(!mkdir("Overlay/Images",0700));
    PutByte("Overlay/Images/Loose.bmp",'i');
    CHECK(OsPosix_SetArchiveLanguage("tc"));
    CHECK(OsPosix_SetPatchDirectory(patch));
    ExpectArchive("system.arc",'p');
    ExpectArchive("C:\\SYSTEM.ARC",'p');
    ExpectArchive("system.tc.arc",'o');
    ExpectArchive("data00010.arc",'d');
    ExpectArchive("data00020.arc",'e');
    ExpectArchive("images\\loose.bmp",'i');
    CHECK(OS_FileAttrs("images") & OS_ATTR_DIRECTORY);
    CHECK(!OS_FileOpenRead("../system.arc"));
    OsFile_t* save = OS_FileCreate("save.dat");
    CHECK(save && OS_FileWrite(save,"x",1)==1);
    OS_FileClose(save);
    snprintf(path,sizeof path,"%s/save.dat",patch);
    CHECK(access("save.dat",F_OK)==0 && access(path,F_OK)!=0);
    CHECK(OsPosix_SetPatchDirectory(""));
    ExpectArchive("system.arc",'o');
    CHECK(!OS_FileOpenRead("data00020.arc"));
    CHECK(OsPosix_SetArchiveLanguage(""));
    CHECK(!unlink("Overlay/Images/Loose.bmp") && !rmdir("Overlay/Images"));
    CHECK(!unlink("Overlay/System.ARC") && !unlink("Overlay/DATA00020.ARC") && !rmdir(patch));
    CHECK(!unlink("SYSTEM.TC.ARC") && !unlink("Data00010.arc") && !unlink("save.dat"));
    puts("PASS posixpaths: patch overrides files and merges searches");
}

static void ReadOnlyGame(void)
{
    char data[] = "/tmp/openbgi-private-data-XXXXXX", path[PATH_MAX];
    CHECK(mkdtemp(data));
    snprintf(path,sizeof path,"%s/.openbgi",data);CHECK(!mkdir(path,0700));
    snprintf(path,sizeof path,"%s/.openbgi/original-saves.txt",data);
    FILE* index = fopen(path,"wb");
    CHECK(index && fputs("bgi.gdb\n",index) >= 0 && !fclose(index));
    snprintf(path,sizeof path,"%s/BGI.GDB",data);PutByte(path,'p');
    PutByte("BGI.GDB",'o');PutByte("common.dat",'b');
    struct stat before,after;
    CHECK(!chmod(".",0555) && !stat(".",&before));
    CHECK(OsPosix_SetDataDirectory(data));
    ExpectArchive("bgi.gdb",'p');
    CHECK(OS_FileDelete("BGI.gdb"));
    CHECK(!OS_FileOpenRead("C:\\bgi.gdb") && OS_FileAttrs("bgi.gdb")==OS_INVALID_ATTRS);
    OsFindData_t entry;
    CHECK(!OS_FindFirst("bgi.*",&entry));
    CHECK(!OS_FileDelete("common.dat") && !OS_FileSetAttrs("common.dat",OS_ATTR_READONLY));
    OsFile_t* file = OS_FileOpenRead("common.dat");
    CHECK(file && !OS_FileSetTime(file,0,0));OS_FileClose(file);
    file = OS_FileOpenWrite("C:\\Common.dat");
    CHECK(file && OS_FileSize(file)==1 && OS_FileWrite(file,"c",1)==1);
    OS_FileClose(file);ExpectArchive("common.dat",'c');
    CHECK(OS_DirCreate("saves"));
    CHECK(OS_FileCopy("common.dat","saves\\new.sud",1));
    CHECK(OS_FileMove("saves\\new.sud","saves\\moved.sud"));
    ExpectArchive("saves\\moved.sud",'c');
    CHECK(!OS_FileCreate("missing\\new.sud"));
    CHECK(!OS_FileCreate("../cannot-write.sud"));
    CHECK(!OS_FileCreate(".openbgi\\locked"));
    file = OS_FileCreate("archive\\Private.dat");
    CHECK(file && OS_FileWrite(file,"w",1)==1);OS_FileClose(file);
    ExpectArchive("archive\\private.dat",'w');
    CHECK(OS_FileDelete("archive\\private.dat") && OS_DirRemove("archive"));
    CHECK(!stat(".",&after) && before.st_mtim.tv_sec==after.st_mtim.tv_sec && before.st_mtim.tv_nsec==after.st_mtim.tv_nsec);
    CHECK(access("saves",F_OK)!=0 && access("archive/Private.dat",F_OK)!=0);
    CHECK(OS_FileDelete("saves\\moved.sud") && OS_DirRemove("saves") && OS_FileDelete("common.dat"));
    CHECK(OsPosix_SetDataDirectory(""));
    ExpectArchive("common.dat",'b');ExpectArchive("bgi.gdb",'o');
    CHECK(!chmod(".",0700) && !unlink("BGI.GDB") && !unlink("common.dat"));
    snprintf(path,sizeof path,"%s/.openbgi/original-saves.txt",data);CHECK(!unlink(path));
    snprintf(path,sizeof path,"%s/.openbgi",data);CHECK(!rmdir(path) && !rmdir(data));
    puts("PASS posixpaths: read-only game, private writes, migrated saves, unchanged source");
}

static size_t DiscRecord(uint8_t* record, const char* name, uint32_t sector, uint32_t size, int directory)
{
    size_t n=strlen(name), bytes=33+n+(n%2==0);
    memset(record,0,bytes);
    record[0]=(uint8_t)bytes;
    PutLe32(record+2,sector);PutBe32(record+6,sector);
    PutLe32(record+10,size);PutBe32(record+14,size);
    record[18]=124;record[19]=6;record[20]=1;
    record[25]=directory?2:0;
    record[28]=record[31]=1;
    record[32]=(uint8_t)n;memcpy(record+33,name,n);
    return bytes;
}

static void ReadDisc(void)
{
    enum { Sector=2048 };
    const uint32_t far=0x200001;
    uint8_t index[5*Sector]={0};
    FILE* f=fopen("fixture.iso","wb");
    CHECK(f);
    index[0]=1;memcpy(index+1,"CD001\1",6);
    DiscRecord(index+156,".",18,Sector,1);
    index[Sector]=255;memcpy(index+Sector+1,"CD001\1",6);
    DiscRecord(index+2*Sector,"GAME",19,Sector,1);
    size_t pos=3*Sector;
    pos+=DiscRecord(index+pos,"IPL._BP;1",21,1,0);
    pos+=DiscRecord(index+pos,"SYSTEM.ARC;1",21,1,0);
    pos+=DiscRecord(index+pos,"SYSTEM.CN.ARC;1",far,3,0);
    DiscRecord(index+pos,"MEDIA",20,Sector,1);
    DiscRecord(index+4*Sector,"DATA.DAT;1",far,2,0);
    CHECK(!fseeko(f,16*Sector,SEEK_SET) && fwrite(index,1,sizeof index,f)==sizeof index);
    CHECK(fputc('o',f)=='o');
    CHECK(!fseeko(f,(off_t)far*Sector,SEEK_SET) && fwrite("new",1,3,f)==3 && !fclose(f));

    CHECK(OsPosix_MountDisc("fixture.iso"));
    /* The launcher's read-only view reports the same index the file services use:
     * the game root is GAME/, extents beyond 4 GiB stay 64-bit, versions are stripped. */
    CHECK(Disc_Count()==6 && !strcmp(Disc_ImagePath(),"fixture.iso") && !strcmp(Disc_Root(),"GAME/"));
    {
        int seen=0;
        for(uint32_t i=0;i<Disc_Count();++i)
        {
            const VolEntry_t* entry=Disc_At(i);
            CHECK(entry && entry->path && entry->data<=((uint64_t)far*Sector+3) &&
                entry->size<=((uint64_t)far*Sector+3)-entry->data);
            if(!strcmp(entry->path,"GAME/IPL._BP")){seen|=1;CHECK(entry->size==1 && entry->readable && entry->stored && !entry->isDir);}
            if(!strcmp(entry->path,"GAME/SYSTEM.CN.ARC")){seen|=2;CHECK(entry->size==3 && entry->data==(uint64_t)far*Sector);}
            if(!strcmp(entry->path,"GAME/MEDIA/DATA.DAT")){seen|=4;CHECK(entry->size==2);}
            if(!strcmp(entry->path,"GAME/MEDIA")){seen|=8;CHECK(entry->isDir);}
        }
        CHECK(seen==15 && !Disc_At(Disc_Count()) && Disc_At(0) && Disc_At(0)->isDir && !strcmp(Disc_At(0)->path,"GAME"));
    }
    CHECK(OS_FileAttrs("C:\\ipl._bp")==OS_ATTR_READONLY && (OS_FileAttrs("C:\\media")&OS_ATTR_DIRECTORY));
    CHECK(OS_FileAttrs("C:\\game\\ipl._bp")==OS_INVALID_ATTRS);
    ExpectArchive("C:\\system.arc",'o');
    CHECK(OsPosix_SetArchiveLanguage("cn"));
    OsFile_t* file=OS_FileOpenRead("C:\\system.arc");
    char data[4]={0};
    CHECK(file && OS_FileSize(file)==3 && OS_FileRead(file,data,4)==3 && !strcmp(data,"new"));
    CHECK(OS_FileSeek(file,1) && OS_FileRead(file,data,4)==2 && !memcmp(data,"ew",2));
    CHECK(!OS_FileSeek(file,4) && !OS_FileWrite(file,"X",1) && !OS_FileSetTime(file,0,0));
    OS_FileClose(file);
    OsFindData_t item;
    OsFind_t* find=OS_FindFirst("C:\\*.arc",&item);
    CHECK(find && !strcasecmp(item.name,"system.arc") && !OS_FindNext(find,&item));OS_FindClose(find);
    find=OS_FindFirst("C:\\media\\*.dat",&item);
    CHECK(find && !strcasecmp(item.name,"data.dat") && !OS_FindNext(find,&item));OS_FindClose(find);
    CHECK(OS_FileCopy("C:\\media\\data.dat","disc-copy.dat",1));
    file=OS_FileOpenRead("disc-copy.dat");
    CHECK(file && OS_FileSize(file)==2 && OS_FileRead(file,data,4)==2 && !memcmp(data,"ne",2));OS_FileClose(file);
    CHECK(!unlink("disc-copy.dat"));
    char layer[PATH_MAX], config[PATH_MAX], saves[PATH_MAX];
    snprintf(layer,sizeof layer,"%s/DiscData",gPosixExeDir);
    snprintf(config,sizeof config,"%s/.openbgi",layer);
    snprintf(saves,sizeof saves,"%s/original-saves.txt",config);
    CHECK(!mkdir(layer,0700) && !mkdir(config,0700));
    Touch(saves);
    CHECK(OsPosix_SetDataDirectory(layer));
    file=OS_FileCreate("C:\\media\\slot.sud");
    CHECK(file && OS_FileWrite(file,"s",1)==1);OS_FileClose(file);
    CHECK(OS_FileDelete("C:\\media\\slot.sud") && OS_DirRemove("C:\\media"));
    CHECK(access("media",F_OK)!=0 && OsPosix_SetDataDirectory("") && !unlink(saves) && !rmdir(config) && !rmdir(layer));
    file=OS_FileOpenRead("C:\\ipl._bp");
    CHECK(file && OsPosix_MountDisc(NULL) && OS_FileRead(file,data,4)==1 && data[0]=='o');OS_FileClose(file);
    CHECK(!OS_FileOpenRead("C:\\ipl._bp") && !unlink("fixture.iso"));
    CHECK(OsPosix_SetArchiveLanguage(""));
    puts("PASS posixpaths: read-only ISO, 64-bit extent, localized lookup, enumeration and copy");
}

// ---- ZIP archives ----------------------------------------------------------------------------------

#define SIG_LOCAL     0x04034b50u
#define SIG_CENTRAL   0x02014b50u
#define SIG_END       0x06054b50u
#define SIG_END64     0x06064b50u
#define SIG_LOCATOR64 0x07064b50u

static void PutLe64At(uint8_t* p, uint64_t v)
{
    PutLe32(p, (uint32_t)v);
    PutLe32(p + 4, (uint32_t)(v >> 32));
}

typedef struct ZipSpec
{
    const char* name;   // as stored, '/' separated; a trailing '/' marks a directory
    const char* text;
    int method;         // 0 stored, 8 deflated, anything else unsupported here
    int badCrc;         // write a check value that cannot match
    int zip64;          // sizes and local offset in a zip64 extra field
    int flags;          // general purpose bits; 1 marks an encrypted entry
} ZipSpec_t;

/* A minimal archive writer: local headers, the central directory, and the
 * end records (the zip64 pair too, when asked). */
static void WriteZip(const char* path, const ZipSpec_t* files, size_t count, int zip64Directory, off_t start)
{
    FILE* f = fopen(path, "wb");
    off_t* local = calloc(count, sizeof *local);
    uint8_t** owned = calloc(count, sizeof *owned);
    size_t* csize = calloc(count, sizeof *csize);
    uint32_t* crc = calloc(count, sizeof *crc);
    off_t cdStart, cdSize;
    CHECK(f && local && owned && csize && crc);
    CHECK(!fseeko(f, start, SEEK_SET));
    for(size_t i = 0; i < count; i++)
    {
        size_t usize = strlen(files[i].text), stored = usize;
        const uint8_t* data = (const uint8_t*)files[i].text;
        crc[i] = BGI_Crc32(0, files[i].text, usize) + (files[i].badCrc ? 1u : 0u);
        if(files[i].method == 8 && usize)
        {
            // A zlib stream minus its header and Adler-32 is raw deflate.
            size_t z = 0;
            uint8_t* stream = Deflate_Compress(data, usize, 6, &z);
            CHECK(stream && z > 6 && stream[0] == 0x78 && !(stream[1] & 0x20));
            owned[i] = malloc(z - 6);
            CHECK(owned[i]);
            memcpy(owned[i], stream + 2, z - 6);
            free(stream);
            data = owned[i];
            stored = z - 6;
        }
        csize[i] = stored;
        local[i] = ftello(f);
        {
            uint8_t h[30] = {0};
            PutLe32(h, SIG_LOCAL);
            PutLe16(h + 4, 20);
            PutLe16(h + 6, (uint32_t)files[i].flags);
            PutLe16(h + 8, (uint32_t)files[i].method);
            PutLe32(h + 14, crc[i]);
            PutLe32(h + 18, (uint32_t)stored);
            PutLe32(h + 22, (uint32_t)usize);
            PutLe16(h + 26, (uint32_t)strlen(files[i].name));
            CHECK(fwrite(h, 1, sizeof h, f) == sizeof h);
            CHECK(fwrite(files[i].name, 1, strlen(files[i].name), f) == strlen(files[i].name));
            CHECK(fwrite(data, 1, stored, f) == stored);
        }
    }
    cdStart = ftello(f);
    for(size_t i = 0; i < count; i++)
    {
        size_t nlen = strlen(files[i].name), elen = files[i].zip64 ? 28 : 0;
        uint8_t h[46] = {0}, x[28] = {0};
        PutLe32(h, SIG_CENTRAL);
        PutLe16(h + 4, 45);
        PutLe16(h + 6, 45);
        PutLe16(h + 8, (uint32_t)files[i].flags);
        PutLe16(h + 10, (uint32_t)files[i].method);
        PutLe32(h + 16, crc[i]);
        if(files[i].zip64)
        {
            PutLe32(h + 20, 0xffffffffu);
            PutLe32(h + 24, 0xffffffffu);
            PutLe32(h + 42, 0xffffffffu);
            PutLe16(x, 1);
            PutLe16(x + 2, 24);
            PutLe64At(x + 4, strlen(files[i].text));
            PutLe64At(x + 12, csize[i]);
            PutLe64At(x + 20, (uint64_t)local[i]);
        }
        else
        {
            PutLe32(h + 20, (uint32_t)csize[i]);
            PutLe32(h + 24, (uint32_t)strlen(files[i].text));
            PutLe32(h + 42, (uint32_t)local[i]);
        }
        PutLe16(h + 28, (uint32_t)nlen);
        PutLe16(h + 30, (uint32_t)elen);
        CHECK(fwrite(h, 1, sizeof h, f) == sizeof h);
        CHECK(fwrite(files[i].name, 1, nlen, f) == nlen);
        CHECK(fwrite(x, 1, elen, f) == elen);
    }
    cdSize = ftello(f) - cdStart;
    if(zip64Directory)
    {
        uint8_t e[56] = {0}, loc[20] = {0};
        off_t end64 = ftello(f);
        PutLe32(e, SIG_END64);
        PutLe64At(e + 4, 44);
        PutLe16(e + 12, 45);
        PutLe16(e + 14, 45);
        PutLe64At(e + 24, (uint64_t)count);
        PutLe64At(e + 32, (uint64_t)count);
        PutLe64At(e + 40, (uint64_t)cdSize);
        PutLe64At(e + 48, (uint64_t)cdStart);
        CHECK(fwrite(e, 1, sizeof e, f) == sizeof e);
        PutLe32(loc, SIG_LOCATOR64);
        PutLe64At(loc + 8, (uint64_t)end64);
        PutLe32(loc + 16, 1);
        CHECK(fwrite(loc, 1, sizeof loc, f) == sizeof loc);
    }
    {
        uint8_t e[22] = {0};
        PutLe32(e, SIG_END);
        PutLe16(e + 8, zip64Directory ? 0xffffu : (uint32_t)count);
        PutLe16(e + 10, zip64Directory ? 0xffffu : (uint32_t)count);
        PutLe32(e + 12, zip64Directory ? 0xffffffffu : (uint32_t)cdSize);
        PutLe32(e + 16, zip64Directory ? 0xffffffffu : (uint32_t)cdStart);
        CHECK(fwrite(e, 1, sizeof e, f) == sizeof e);
    }
    CHECK(!fclose(f));
    for(size_t i = 0; i < count; i++)
        free(owned[i]);
    free(owned);
    free(csize);
    free(crc);
    free(local);
}

static const char kArcText[] = "deflated-arc-0123456789-deflated-arc-0123456789";

static void ReadZip(void)
{
    static const ZipSpec_t files[] = {
        {"Game/IPL._BP", "o", 0, 0, 0, 0},
        {"Game/SYSTEM.ARC", kArcText, 8, 0, 0, 0},
        {"Game/SYSTEM.CN.ARC", "cn", 0, 0, 0, 0},
        {"Game/MEDIA/DATA.DAT", "data-data", 8, 0, 0, 0},
        {"Game/README.TXT", "readme", 0, 0, 0, 0},
        {"Game/BROKEN.DAT", "broken", 8, 1, 0, 0},
        {"Game/dir/", "", 0, 0, 0, 0},
        {"__MACOSX/._junk", "x", 0, 0, 0, 0},
        {"../escape.txt", "x", 0, 0, 0, 0},
        /* Two entries the reader keeps in the index but cannot open: another
         * compression method, and an entry with the encryption bit set. */
        {"Game/PACKED.DAT", "packed", 12, 0, 0, 0},
        {"Game/SECRET.DAT", "secret", 0, 0, 0, 1},
    };
    struct stat st;
    const VolEntry_t *ipl, *sys;
    OsFindData_t item;
    OsFind_t* find;
    OsFile_t* file;
    char buf[128] = {0};
    WriteZip("fixture.zip", files, sizeof files / sizeof files[0], 0, 0);
    CHECK(!stat("fixture.zip", &st));
    CHECK(OsPosix_MountDisc("fixture.zip"));
    /* The game root is the wrapper folder; macOS metadata and names that
     * leave the root are dropped, and the directories the archive does not
     * list (MEDIA) are added from their files. */
    CHECK(Disc_Count() == 11 && !strcmp(Disc_ImagePath(), "fixture.zip") && !strcmp(Disc_Root(), "Game/"));
    for(uint32_t i = 0; i < Disc_Count(); ++i)
    {
        const VolEntry_t* entry = Disc_At(i);
        CHECK(entry && entry->path && entry->data <= (uint64_t)st.st_size &&
            entry->csize <= (uint64_t)st.st_size - entry->data);
    }
    ipl = Disc_Find("IPL._BP");
    sys = Disc_Find("SYSTEM.ARC");
    CHECK(ipl && ipl->stored && ipl->method == 0 && ipl->size == 1 && ipl->csize == 1);
    CHECK(sys && !sys->stored && sys->method == 8 && sys->size == strlen(kArcText) && sys->csize < sys->size);
    CHECK(Disc_Find("MEDIA") && Disc_Find("MEDIA")->isDir && Disc_Find("MEDIA")->readable == 0);
    CHECK(!Disc_Find("ESCAPE.TXT"));
    CHECK(OS_FileAttrs("C:\\media") & OS_ATTR_DIRECTORY);
    CHECK(OS_FileAttrs("C:\\dir") & OS_ATTR_DIRECTORY);
    CHECK(OS_FileAttrs("C:\\readme.txt") == OS_ATTR_READONLY);
    ExpectArchive("system.arc", 'd');
    CHECK(OsPosix_SetArchiveLanguage("cn"));
    ExpectArchive("system.arc", 'c');
    CHECK(OsPosix_SetArchiveLanguage(""));
    /* A deflated entry reads, sizes and seeks through the file services. */
    file = OS_FileOpenRead("C:\\system.arc");
    CHECK(file && OS_FileSize(file) == strlen(kArcText) && OS_FileRead(file, buf, sizeof buf) == strlen(kArcText) &&
        !memcmp(buf, kArcText, strlen(kArcText)));
    CHECK(OS_FileSeek(file, 5) && OS_FileRead(file, buf, 3) == 3 && !memcmp(buf, "ted", 3));
    CHECK(!OS_FileWrite(file, "x", 1) && !OS_FileSetTime(file, 0, 0));
    OS_FileClose(file);
    /* The decompressed copy outlives the mount, as a stored entry's descriptor does. */
    file = OS_FileOpenRead("C:\\system.arc");
    CHECK(file && OsPosix_MountDisc(NULL) && OS_FileRead(file, buf, 4) == 4 && !memcmp(buf, "defl", 4));
    OS_FileClose(file);
    CHECK(OsPosix_MountDisc("fixture.zip"));
    CHECK(OS_FileCopy("C:\\media\\data.dat", "zip-copy.dat", 1));
    file = OS_FileOpenRead("zip-copy.dat");
    CHECK(file && OS_FileSize(file) == 9 && OS_FileRead(file, buf, 16) == 9 && !memcmp(buf, "data-data", 9));
    OS_FileClose(file);
    CHECK(!unlink("zip-copy.dat"));
    /* A wrong check value is reported when the bytes are wanted, not hidden. */
    CHECK(OS_FileAttrs("C:\\broken.dat") == OS_ATTR_READONLY && !OS_FileOpenRead("C:\\broken.dat"));
    /* An entry compressed another way, or an encrypted one, keeps its place in
     * the index so the launcher can name it - but no file service opens it. */
    {
        const VolEntry_t* packed = NULL;
        const VolEntry_t* secret = NULL;
        DiscHandle_t handle;
        for(uint32_t i = 0; i < Disc_Count(); ++i)
        {
            const VolEntry_t* entry = Disc_At(i);
            if(entry && !strcasecmp(entry->path, "Game/PACKED.DAT"))
                packed = entry;
            if(entry && !strcasecmp(entry->path, "Game/SECRET.DAT"))
                secret = entry;
        }
        CHECK(packed && !packed->readable && packed->method == 12 && !packed->stored && !packed->encrypted);
        CHECK(secret && !secret->readable && secret->method == 0 && !secret->stored && secret->encrypted);
        CHECK(!Disc_Open(packed, &handle) && !Disc_Open(secret, &handle));
        CHECK(!OS_FileOpenRead("C:\\packed.dat") && !OS_FileOpenRead("C:\\secret.dat"));
    }
    find = OS_FindFirst("C:\\readme.*", &item);
    CHECK(find && !strcasecmp(item.name, "readme.txt") && !OS_FindNext(find, &item));
    OS_FindClose(find);
    find = OS_FindFirst("C:\\media\\*.dat", &item);
    CHECK(find && !strcasecmp(item.name, "data.dat") && !OS_FindNext(find, &item));
    OS_FindClose(find);
    CHECK(OsPosix_MountDisc(NULL) && !unlink("fixture.zip"));
    puts("PASS posixpaths: ZIP archive with stored and deflated entries, synthesized directories and CRC checking");
}

static void ReadZip64(void)
{
    static const ZipSpec_t files[] = {
        {"Game/IPL._BP", "z", 0, 0, 1, 0},
        {"Game/SYSTEM.ARC", "s", 8, 0, 1, 0},
    };
    off_t start = (off_t)(UINT64_C(1) << 32) + 1024;
    WriteZip("fixture64.zip", files, sizeof files / sizeof files[0], 1, start);
    CHECK(OsPosix_MountDisc("fixture64.zip"));
    CHECK(Disc_Count() == 3 && !strcmp(Disc_ImagePath(), "fixture64.zip") && !strcmp(Disc_Root(), "Game/"));
    const VolEntry_t* entry = Disc_Find("system.arc");
    CHECK(entry && entry->data > UINT32_MAX);
    ExpectArchive("C:\\ipl._bp", 'z');
    ExpectArchive("C:\\system.arc", 's');
    CHECK(OsPosix_MountDisc(NULL) && !unlink("fixture64.zip"));
    puts("PASS posixpaths: ZIP64 directory and stored/deflated entries beyond 4 GiB");
}

static void ReadHighlyCompressedZip(void)
{
    char contents[4097], readback[4096];
    memset(contents, 'a', sizeof contents - 1);
    contents[sizeof contents - 1] = 0;
    const ZipSpec_t files[] = {{"SYSTEM.ARC", contents, 8, 0, 0, 0}};
    WriteZip("compressed.zip", files, 1, 0, 0);
    struct stat st;
    CHECK(!stat("compressed.zip", &st) && st.st_size < (off_t)sizeof readback);
    CHECK(OsPosix_MountDisc("compressed.zip"));
    OsFile_t* file = OS_FileOpenRead("C:\\system.arc");
    CHECK(file && OS_FileRead(file, readback, sizeof readback) == sizeof readback);
    CHECK(!memcmp(readback, contents, sizeof readback));
    OS_FileClose(file);
    CHECK(OsPosix_MountDisc(NULL) && !unlink("compressed.zip"));
    puts("PASS posixpaths: a deflated resource larger than its entire ZIP");
}

/* A translated distribution need not ship the shared system.arc: the wrapper
 * folder may hold only the localized pair. The root must still be found, or
 * the launcher sees "Game/system.tc.arc" and calls the container unbootable. */
static void ReadLocalizedZip(void)
{
    static const ZipSpec_t files[] = {
        {"Game/System.Tc.Arc", "tc-script", 0, 0, 0, 0},
        {"Game/sysprg.tc.arc", "tc-program", 0, 0, 0, 0},
    };
    const VolEntry_t *script, *program;
    WriteZip("localized.zip", files, sizeof files / sizeof files[0], 0, 0);
    CHECK(OsPosix_MountDisc("localized.zip"));
    CHECK(!strcmp(Disc_Root(), "Game/"));
    /* The wrapper itself plus the two files; the launcher only ever sees the
     * two remaining paths once the root is stripped. */
    CHECK(Disc_Count() == 3);
    script = Disc_Find("SYSTEM.TC.ARC");
    program = Disc_Find("SYSPRG.TC.ARC");
    CHECK(script && script->readable && script->size == 9);
    CHECK(program && program->readable && program->size == 10);
    /* The logical name still resolves to the localized entry's bytes. */
    CHECK(OsPosix_SetArchiveLanguage("tc"));
    ExpectArchive("C:\\system.arc", 't');
    ExpectArchive("C:\\sysprg.arc", 't');
    CHECK(OsPosix_SetArchiveLanguage(""));
    CHECK(OsPosix_MountDisc(NULL) && !unlink("localized.zip"));

    /* One level deeper, and with an incomplete pair: the marker still names
     * the root; whether the pair is complete is the launcher's call. */
    {
        static const ZipSpec_t nested[] = {
            {"Outer/Game/SYSTEM.TC.ARC", "tc-script", 0, 0, 0, 0},
            {"Outer/Game/SYSPRG.TC.ARC", "tc-program", 0, 0, 0, 0},
        };
        static const ZipSpec_t incomplete[] = {
            {"Game/SYSTEM.EN.ARC", "en-script", 0, 0, 0, 0},
        };
        WriteZip("nested.zip", nested, sizeof nested / sizeof nested[0], 0, 0);
        CHECK(OsPosix_MountDisc("nested.zip") && !strcmp(Disc_Root(), "Outer/Game/"));
        CHECK(Disc_Find("SYSTEM.TC.ARC") && Disc_Find("SYSPRG.TC.ARC"));
        CHECK(OsPosix_MountDisc(NULL) && !unlink("nested.zip"));
        WriteZip("incomplete.zip", incomplete, 1, 0, 0);
        CHECK(OsPosix_MountDisc("incomplete.zip") && !strcmp(Disc_Root(), "Game/"));
        CHECK(OsPosix_MountDisc(NULL) && !unlink("incomplete.zip"));
    }
    puts("PASS posixpaths: a localized-only wrapper still marks the game root in a ZIP");
}

static void ReadLocalizedDisc(void)
{
    enum { Sector = 2048 };
    uint8_t index[5 * Sector] = {0};
    FILE* f = fopen("localized.iso", "wb");
    CHECK(f);
    index[0] = 1;memcpy(index + 1, "CD001\1", 6);
    DiscRecord(index + 156, ".", 18, Sector, 1);
    index[Sector] = 255;memcpy(index + Sector + 1, "CD001\1", 6);
    DiscRecord(index + 2 * Sector, "GAME", 19, Sector, 1);
    size_t pos = 3 * Sector;
    pos += DiscRecord(index + pos, "SYSTEM.TC.ARC;1", 21, 9, 0);
    DiscRecord(index + pos, "SYSPRG.TC.ARC;1", 22, 10, 0);
    CHECK(!fseeko(f, 16 * Sector, SEEK_SET) && fwrite(index, 1, sizeof index, f) == sizeof index);
    CHECK(!fseeko(f, (off_t)21 * Sector, SEEK_SET) && fwrite("tc-script", 1, 9, f) == 9);
    CHECK(!fseeko(f, (off_t)22 * Sector, SEEK_SET) && fwrite("tc-program", 1, 10, f) == 10);
    CHECK(!fclose(f));
    CHECK(OsPosix_MountDisc("localized.iso"));
    CHECK(!strcmp(Disc_Root(), "GAME/") && Disc_Count() == 3);
    CHECK(Disc_Find("SYSTEM.TC.ARC") && Disc_Find("SYSPRG.TC.ARC"));
    CHECK(OsPosix_SetArchiveLanguage("tc"));
    ExpectArchive("C:\\system.arc", 't');
    CHECK(OsPosix_SetArchiveLanguage(""));
    CHECK(OsPosix_MountDisc(NULL) && !unlink("localized.iso"));
    puts("PASS posixpaths: a localized-only wrapper still marks the game root in an ISO");
}

int main(void)
{
    /* A container written by another generator (the UDF sample, say) can be
     * asked for the root alone: BGI_DISC_ROOT=<path> prints it and stops. */
    {
        const char* probe = getenv("BGI_DISC_ROOT");
        if(probe && *probe)
        {
            CHECK(OsPosix_MountDisc(probe));
            puts(Disc_Root());
            return 0;
        }
    }
    char root[PATH_MAX],cwd[PATH_MAX],path[512],query[512];    const char* temp=getenv("TMPDIR");
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
    LocalizedArchives();
    PatchOverlay();
    ReadOnlyGame();
    ReadDisc();
    ReadZip();
    ReadZip64();
    ReadHighlyCompressedZip();
    ReadLocalizedZip();
    ReadLocalizedDisc();

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
