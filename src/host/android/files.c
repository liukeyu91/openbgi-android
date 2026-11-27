/* SPDX-License-Identifier: GPL-2.0-only */
#include "files.h"
#include "../../os/posix/posix_internal.h"

typedef struct {
    OsFile_t* game;
    FILE* native;
} Stream;

static Stream* NewStream(OsFile_t* game, FILE* native)
{
    if(!game && !native)
        return NULL;
    Stream* stream = malloc(sizeof *stream);
    if(!stream) {
        OS_FileClose(game);
        if(native) fclose(native);
        return NULL;
    }
    *stream = (Stream){.game = game, .native = native};
    return stream;
}

static void* Open(void* user, const char* path, int mode)
{
    (void)user;
    switch(mode) {
        case OPENBGI_OPEN_READ: return NewStream(OS_FileOpenRead(path), NULL);
        case OPENBGI_OPEN_CREATE: return NewStream(OS_FileCreate(path), NULL);
        case OPENBGI_OPEN_WRITE: return NewStream(OS_FileOpenWrite(path), NULL);
        default: return NULL;
    }
}

static void Close(void* user, void* file)
{
    (void)user;
    Stream* stream = file;
    if(!stream) return;
    if(stream->native) fclose(stream->native);
    else OS_FileClose(stream->game);
    free(stream);
}

/* A stream is NULL when the open failed.  The engine treats that as "cannot
 * open", but a call that slips through must not dereference it: a null read
 * was a SIGSEGV at address 0 in the field. */
static uint32_t Read(void* user, void* file, void* buf, uint32_t size)
{
    (void)user;
    Stream* stream = file;
    if(!stream) return 0;
    return stream->game ? OS_FileRead(stream->game, buf, size) : (uint32_t)fread(buf, 1, size, stream->native);
}

static uint32_t Write(void* user, void* file, const void* buf, uint32_t size)
{
    (void)user;
    Stream* stream = file;
    if(!stream) return 0;
    return stream->game ? OS_FileWrite(stream->game, buf, size) : (uint32_t)fwrite(buf, 1, size, stream->native);
}

static int Seek(void* user, void* file, uint64_t position)
{
    (void)user;
    Stream* stream = file;
    if(!stream) return 0;
    if(stream->game)
        return position <= UINT32_MAX && OS_FileSeek(stream->game, (uint32_t)position);
    return position <= INT64_MAX && OS_NativeSeek(stream->native, position);
}

static uint64_t Length(void* user, void* file)
{
    (void)user;
    Stream* stream = file;
    if(!stream) return 0;
    if(stream->game) return OS_FileSize(stream->game);
    struct stat st;
    return fflush(stream->native) == 0 && fstat(fileno(stream->native), &st) == 0 ? (uint64_t)st.st_size : 0;
}

static int GetTime(void* user, void* file, uint32_t* lo, uint32_t* hi)
{
    (void)user;
    Stream* stream = file;
    return stream && stream->game && OS_FileGetTime(stream->game, lo, hi);
}

static int SetTime(void* user, void* file, uint32_t lo, uint32_t hi)
{
    (void)user;
    Stream* stream = file;
    return stream && stream->game && OS_FileSetTime(stream->game, lo, hi);
}

static uint32_t Attrs(void* user, const char* path) { (void)user; return OS_FileAttrs(path); }
static int SetAttrs(void* user, const char* path, uint32_t attrs) { (void)user; return OS_FileSetAttrs(path, attrs); }
static int Remove(void* user, const char* path) { (void)user; return OS_FileDelete(path); }
static int Move(void* user, const char* from, const char* to) { (void)user; return OS_FileMove(from, to); }
static int Copy(void* user, const char* from, const char* to, int exclusive) { (void)user; return OS_FileCopy(from, to, exclusive); }
static int MakeDir(void* user, const char* path) { (void)user; return OS_DirCreate(path); }
static int RemoveDir(void* user, const char* path) { (void)user; return OS_DirRemove(path); }

static void FindResult(const OsFindData_t* entry, OpenBGI_FindData_t* out)
{
    out->attrs = entry->attrs;
    if(OsPosix_Utf8Paths())
        snprintf(out->name, sizeof out->name, "%s", entry->name);
    else
        OS_Utf8ToSjis(entry->name, out->name, sizeof out->name);
}

static void* FindFirst(void* user, const char* pattern, OpenBGI_FindData_t* out)
{
    (void)user;
    OsFindData_t entry;
    OsFind_t* find = OS_FindFirst(pattern, &entry);
    if(find) FindResult(&entry, out);
    return find;
}

static int FindNext(void* user, void* find, OpenBGI_FindData_t* out)
{
    (void)user;
    OsFindData_t entry;
    if(!OS_FindNext(find, &entry)) return 0;
    FindResult(&entry, out);
    return 1;
}

static void FindClose(void* user, void* find) { (void)user; OS_FindClose(find); }
static int SetCurrentDir(void* user, const char* path) { (void)user; return OS_SetCurrentDir(path); }
static void ExeDir(void* user, char* out, size_t n) { (void)user; OS_ExeDir(out, n); }
static uint32_t DriveType(void* user, const char* path) { (void)user; return OS_DriveType(path); }
static uint32_t LogicalDrives(void* user, char* out, uint32_t n) { (void)user; return OS_LogicalDrives(out, n); }
static int VolumeReady(void* user, const char* path) { (void)user; return OS_VolumeReady(path); }
static int MediaPresent(void* user, char drive) { (void)user; return OS_MediaPresent(drive); }

static int InDirectory(const char* dir, const char* file)
{
    size_t n = strlen(dir);
    return n && !strncmp(dir, file, n) && (dir[n - 1] == '/' || file[n] == '/');
}

/* The upstream startup writes a native probe in the game directory. Route
 * it through the same writable layer as the game's own files. */
static int NativeWritePath(const char* path, char* out, size_t n, int parents)
{
    char native[PATH_MAX];
    OS_NativeAbsolute(path, native, sizeof native);
    if(!OsPosix_HasDataDirectory() || InDirectory(gPosixConfigDir, native))
        return snprintf(out, n, "%s", native) < (int)n;
    return OsPosix_WriteNativePath(native, out, n, parents);
}

static void* NativeOpen(void* user, const char* path, int mode)
{
    (void)user;
    if(mode == OPENBGI_OPEN_READ && OS_NativeStat(path, NULL) == OS_NATIVE_FILE)
        return NewStream(NULL, OS_NativeOpen(path, "rb"));
    char writable[PATH_MAX];
    if(mode == OPENBGI_OPEN_CREATE && NativeWritePath(path, writable, sizeof writable, 0))
        return NewStream(NULL, OS_NativeOpen(writable, "wb"));
    return NULL;
}

static int NativeStat(void* user, const char* path, OpenBGI_NativeStat_t* out)
{
    (void)user;
    OsNativeStat_t st;
    int type = OS_NativeStat(path, &st);
    if(out && type != OS_NATIVE_NONE)
        *out = (OpenBGI_NativeStat_t){.size = st.size, .mtime = st.mtime};
    return type;
}

static int NativeMkdir(void* user, const char* path)
{
    (void)user;
    char writable[PATH_MAX];
    return NativeWritePath(path, writable, sizeof writable, 0) && OS_NativeMkdir(writable);
}

static int NativeRemove(void* user, const char* path)
{
    (void)user;
    char writable[PATH_MAX];
    return NativeWritePath(path, writable, sizeof writable, 0) && OS_NativeRemove(writable);
}

static int NativeList(void* user, const char* dir, void (*fn)(void*, const char*, int), void* ctx)
{
    (void)user;
    return OS_NativeList(dir, fn, ctx);
}

static void NativeAbsolute(void* user, const char* path, char* out, size_t n)
{
    (void)user;
    OS_NativeAbsolute(path, out, n);
}

static void GameDir(void* user, char* out, size_t n) { (void)user; OS_GameDir(out, n); }

static int SetGameDir(void* user, const char* path)
{
    (void)user;
    char resolved[PATH_MAX];
    if(!realpath(path, resolved)) return 0;
    size_t size = strlen(resolved);
    /* A library launch already selected the game and its overlays. */
    if(strncmp(resolved, gPosixExeDir, size) || (gPosixExeDir[size] && strcmp(gPosixExeDir + size, "/")))
        return 0;
    return OS_SetGameDir(resolved);
}

static void ConfigDir(void* user, char* out, size_t n)
{
    (void)user;
    snprintf(out, n, "%s/", gPosixConfigDir);
}

const OpenBGI_Files_t AndroidHostFiles = {
    .size = sizeof(OpenBGI_Files_t),
    .open = Open, .close = Close, .read = Read, .write = Write, .seek = Seek, .length = Length,
    .getTime = GetTime, .setTime = SetTime, .attrs = Attrs, .setAttrs = SetAttrs,
    .remove = Remove, .move = Move, .copy = Copy, .makeDir = MakeDir, .removeDir = RemoveDir,
    .findFirst = FindFirst, .findNext = FindNext, .findClose = FindClose, .setCurrentDir = SetCurrentDir,
    .exeDir = ExeDir, .driveType = DriveType, .logicalDrives = LogicalDrives,
    .volumeReady = VolumeReady, .mediaPresent = MediaPresent,
    .nativeSep = '/', .nativeOpen = NativeOpen, .nativeStat = NativeStat, .nativeMkdir = NativeMkdir,
    .nativeRemove = NativeRemove, .nativeList = NativeList, .nativeAbsolute = NativeAbsolute,
    .gameDir = GameDir, .setGameDir = SetGameDir, .userDataDir = ConfigDir, .binaryDir = ConfigDir,
};
