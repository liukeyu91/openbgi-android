/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * files.c - files, directories and drives of the POSIX back end
 *           (inc/bgi/os.h): the OsFile_t operations over stdio, file
 *           attributes and times, directory enumeration, and the one
 *           fixed pseudo drive "C:" that stands for the game directory
 *
 * The path resolver (paths.c) maps the engine's Shift-JIS Windows paths
 * to case-corrected POSIX paths. The Windows attributes are approximated: a directory, the
 * owner's write bit as read-only, a leading '.' as hidden.
 */
#include "disc_internal.h"

// ---- files -------------------------------------------------------------------------------------

struct OsFile
{
	FILE* f;             // the stream
	char path[PATH_MAX]; // the POSIX path it was opened with
	int writable;
	int isDisc;
	DiscHandle_t disc;
	uint64_t discTime;
};

static int OpenDiscEntry(OsFile_t* f)
{
	const VolEntry_t* entry = OsPosix_DiscPath(f->path, 1);
	if(!entry || !Disc_Open(entry, &f->disc))
		return 0;
	f->isDisc = 1;
	f->discTime = entry->mtime;
	return 1;
}

// fopen "rb" of the converted path; NULL when it cannot be opened or is a directory (as CreateFile)
OsFile_t* OS_FileOpenRead(const char* path)
{
	OsFile_t* f = (OsFile_t*)calloc(1, sizeof *f);
	struct stat st;
	if(!f) return NULL;
	OsPosix_ReadPath(path, f->path, sizeof f->path);
	f->f = fopen(f->path, "rb");
	if(!f->f && (errno == ENOENT || errno == ENOTDIR) && OpenDiscEntry(f))
		return f;
	if(f->f && fstat(fileno(f->f), &st) == 0 && S_ISDIR(st.st_mode))
	{
		fclose(f->f);
		f->f = NULL;
	}
	if(!f->f)
	{
		free(f);
		return NULL;
	}
	return f;
}

// fopen "w+b" (truncating) of the converted path; NULL when it cannot be created
OsFile_t* OS_FileCreate(const char* path)
{
	OsFile_t* f = (OsFile_t*)calloc(1, sizeof *f);
	if(!f) return NULL;
	if(!OsPosix_WritePath(path, f->path, sizeof f->path, 1)) { free(f); return NULL; }
	f->f = fopen(f->path, "w+b");
	if(!f->f)
	{
		free(f);
		return NULL;
	}
	f->writable = 1;
	return f;
}

// OPEN_ALWAYS for writing: an existing file keeps its contents (at position 0), a missing one is created; NULL on failure
OsFile_t* OS_FileOpenWrite(const char* path)
{
	OsFile_t* f = (OsFile_t*)calloc(1, sizeof *f);
	if(!f) return NULL;
	if(!OsPosix_WritePath(path, f->path, sizeof f->path, 1)) { free(f); return NULL; }
	f->f = fopen(f->path, "r+b");
	if(!f->f && errno == ENOENT)
	{
		if(OsPosix_HasDataDirectory() && OS_FileAttrs(path) != OS_INVALID_ATTRS)
		{
			if(OS_FileCopy(path, path, 0)) f->f = fopen(f->path, "r+b");
		}
		else f->f = fopen(f->path, "w+b");
	}
	if(!f->f)
	{
		free(f);
		return NULL;
	}
	f->writable = 1;
	return f;
}

void OS_FileClose(OsFile_t* f)
{
	if(!f)
		return;
	if(f->f)
		fclose(f->f);
	else
		Disc_Close(&f->disc);
	free(f);
}

uint32_t OS_FileRead(OsFile_t* f, void* buf, uint32_t n)
{
	if(!f->f)
		return Disc_Read(&f->disc, buf, n);
	return (uint32_t)fread(buf, 1, n, f->f);
}

uint32_t OS_FileWrite(OsFile_t* f, const void* buf, uint32_t n)
{
	return f->f ? (uint32_t)fwrite(buf, 1, n, f->f) : 0;
}

int OS_FileSeek(OsFile_t* f, uint32_t pos)
{
	if(!f->f)
	{
		if(pos > f->disc.size)
			return 0;
		f->disc.position = pos;
		return 1;
	}
	return fseek(f->f, (long)pos, SEEK_SET) == 0;
}

// the size from fstat, cut to 32 bits, with what is still buffered for writing; 0 on failure
uint32_t OS_FileSize(OsFile_t* f)
{
	struct stat st;
	if(!f->f)
		return f->disc.size;
	fflush(f->f);
	if(fstat(fileno(f->f), &st) != 0)
		return 0;
	return (uint32_t)st.st_size;
}

// FILETIME (100 ns units since 1601) <-> time_t: the seconds between the two epochs
#define FILETIME_EPOCH_DIFF 11644473600ull

// the modification time as a FILETIME (whole seconds: st_mtime has no finer resolution here)
int OS_FileGetTime(OsFile_t* f, uint32_t* lo, uint32_t* hi)
{
	struct stat st;
	uint64_t ft;
	if(f->f)
	{
		if(fstat(fileno(f->f), &st) != 0)
			return 0;
		ft = ((uint64_t)st.st_mtime + FILETIME_EPOCH_DIFF) * 10000000ull;
	}
	else
		ft = f->discTime;
	*lo = (uint32_t)ft;
	*hi = (uint32_t)(ft >> 32);
	return 1;
}

// set the access and modification times from a FILETIME, after flushing so that the write does not update them again
int OS_FileSetTime(OsFile_t* f, uint32_t lo, uint32_t hi)
{
	uint64_t ft = ((uint64_t)hi << 32) | lo;
	struct timespec ts[2];
	if(!f->f || (OsPosix_HasDataDirectory() && !f->writable)) return 0;
	fflush(f->f);
	ts[0].tv_sec = ts[1].tv_sec = (time_t)(ft / 10000000ull - FILETIME_EPOCH_DIFF);
	ts[0].tv_nsec = ts[1].tv_nsec = (long)(ft % 10000000ull) * 100;
	return futimens(fileno(f->f), ts) == 0;
}

// UTC calendar records for the script's file metadata services.
static int ToSystemTime(const struct timespec* ts, OsLocalTime_t* out)
{
	struct tm tm;
	if(!gmtime_r(&ts->tv_sec, &tm)) return 0;
	out->year = (uint16_t)(tm.tm_year + 1900); out->month = (uint16_t)(tm.tm_mon + 1);
	out->dayOfWeek = (uint16_t)tm.tm_wday; out->day = (uint16_t)tm.tm_mday;
	out->hour = (uint16_t)tm.tm_hour; out->minute = (uint16_t)tm.tm_min;
	out->second = (uint16_t)tm.tm_sec; out->millis = (uint16_t)(ts->tv_nsec / 1000000);
	return 1;
}

static int FromSystemTime(const OsLocalTime_t* in, struct timespec* ts)
{
	struct tm tm = {0}, check;
	if(in->year < 1601 || in->month < 1 || in->month > 12 || in->day < 1 || in->day > 31 ||
		in->hour > 23 || in->minute > 59 || in->second > 59 || in->millis > 999) return 0;
	tm.tm_year = in->year - 1900; tm.tm_mon = in->month - 1; tm.tm_mday = in->day;
	tm.tm_hour = in->hour; tm.tm_min = in->minute; tm.tm_sec = in->second;
	ts->tv_sec = timegm(&tm); ts->tv_nsec = (long)in->millis * 1000000;
	return gmtime_r(&ts->tv_sec, &check) && check.tm_year == in->year - 1900 &&
		check.tm_mon == in->month - 1 && check.tm_mday == in->day && check.tm_hour == in->hour &&
		check.tm_min == in->minute && check.tm_sec == in->second;
}

int OS_FileGetTimes(const char* path, OsLocalTime_t* created, OsLocalTime_t* accessed, OsLocalTime_t* written)
{
	char native[PATH_MAX];
	struct stat st;
	OsLocalTime_t c, a, w;
	OsPosix_ReadPath(path, native, sizeof native);
	if(stat(native, &st))
	{
		const VolEntry_t* entry = OsPosix_DiscPath(native, 1);
		if(!entry || entry->isDir || !entry->mtime)
			return 0;
		st.st_mode = S_IFREG;
		st.st_mtim.tv_sec = (time_t)((int64_t)(entry->mtime / 10000000ull) - (int64_t)FILETIME_EPOCH_DIFF);
		st.st_mtim.tv_nsec = 0;
		st.st_atim = st.st_mtim;
	}
	if(!S_ISREG(st.st_mode) || !ToSystemTime(&st.st_mtim, &c) ||
		!ToSystemTime(&st.st_atim, &a) || !ToSystemTime(&st.st_mtim, &w)) return 0;
	/* POSIX ctime is a metadata-change time, not a creation time. */
	if(created) memcpy(created, &c, sizeof c);
	if(accessed) memcpy(accessed, &a, sizeof a);
	if(written) memcpy(written, &w, sizeof w);
	return 1;
}

int OS_FileSetTimes(const char* path, const OsLocalTime_t* created, const OsLocalTime_t* accessed, const OsLocalTime_t* written)
{
	char native[PATH_MAX];
	struct stat st;
	struct timespec ts[2], unused;
	if(!OsPosix_WritePath(path, native, sizeof native, 0)) return 0;
	if(stat(native, &st) || !S_ISREG(st.st_mode)) return 0;
	ts[0] = st.st_atim; ts[1] = st.st_mtim;
	if((created && !FromSystemTime(created, &unused)) || (accessed && !FromSystemTime(accessed, &ts[0])) ||
		(written && !FromSystemTime(written, &ts[1]))) return 0;
	return utimensat(AT_FDCWD, native, ts, 0) == 0;
}

// GetFileAttributes: directory, read-only (no owner write bit), hidden (a '.' name); OS_ATTR_NORMAL when none applies
uint32_t OS_FileAttrs(const char* path)
{
	char p[PATH_MAX];
	struct stat st;
	uint32_t a = 0;
	OsPosix_ReadPath(path, p, sizeof p);
	if(stat(p, &st) != 0)
	{
		const VolEntry_t* entry = OsPosix_DiscPath(p, 1);
		if(!entry || (!entry->isDir && (!entry->readable || entry->size > UINT32_MAX)))
			return OS_INVALID_ATTRS;
		return OS_ATTR_READONLY | (entry->isDir ? OS_ATTR_DIRECTORY : 0);
	}
	if(S_ISDIR(st.st_mode))
		a |= OS_ATTR_DIRECTORY;
	if(!(st.st_mode & S_IWUSR))
		a |= OS_ATTR_READONLY;
	if(OsCommon_FilePart(p)[0] == '.')
		a |= OS_ATTR_HIDDEN;
	return a ? a : OS_ATTR_NORMAL;
}

// SetFileAttributes: only the read-only bit has a counterpart (all write bits off, or the owner's back on)
int OS_FileSetAttrs(const char* path, uint32_t attrs)
{
	char p[PATH_MAX];
	struct stat st;
	if(!OsPosix_WritePath(path, p, sizeof p, 0)) return 0;
	if(stat(p, &st) != 0)
		return 0;
	if(attrs & OS_ATTR_READONLY)
		st.st_mode &= (mode_t) ~(S_IWUSR | S_IWGRP | S_IWOTH);
	else
		st.st_mode |= S_IWUSR;
	return chmod(p, st.st_mode) == 0;
}

int OS_FileDelete(const char* path)
{
	char p[PATH_MAX];
	if(!OsPosix_WritePath(path, p, sizeof p, 0)) return 0;
	return unlink(p) == 0;
}

// MoveFile: 0 when `to` exists already (rename() would replace it), unless it is `from` itself under another case
int OS_FileMove(const char* from, const char* to)
{
	char a[PATH_MAX], b[PATH_MAX];
	struct stat sa, sb;
	if(!OsPosix_WritePath(from, a, sizeof a, 0) ||
		!OsPosix_WritePath(to, b, sizeof b, 1)) return 0;
	if(OsPosix_HasDataDirectory() && lstat(b, &sb) != 0 && OS_FileAttrs(to) != OS_INVALID_ATTRS)
		return 0;
	if(lstat(b, &sb) == 0 && !(lstat(a, &sa) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino))
		return 0;
	return rename(a, b) == 0;
}

// CopyFile: a plain copy through a 64 KB buffer; 0 when `to` exists and failIfExists is set, or either file fails to open
int OS_FileCopy(const char* from, const char* to, int failIfExists)
{
	char b[PATH_MAX], buf[0x10000];
	OsFile_t* in;
	FILE* out;
	size_t n;
	int privateCopy = OsPosix_HasDataDirectory();
	if(!OsPosix_WritePath(to, b, sizeof b, 1))
		return 0;
	if(failIfExists && OS_FileAttrs(to) != OS_INVALID_ATTRS)
		return 0;
	in = OS_FileOpenRead(from);
	if(!in)
		return 0;
	struct stat st;
	if(!strcmp(in->path, b) || (in->f && (fstat(fileno(in->f), &st) || !S_ISREG(st.st_mode))))
	{
		OS_FileClose(in);
		return 0;
	}
	int newFile = privateCopy && access(b, F_OK) != 0;
	out = fopen(b, "wb");
	if(!out)
	{
		OS_FileClose(in);
		return 0;
	}
	int ok = 1;
	while((n = OS_FileRead(in, buf, sizeof buf)) > 0)
		if(fwrite(buf, 1, n, out) != n) { ok = 0; break; }
	ok &= in->f ? !ferror(in->f) : in->disc.position == in->disc.size;
	OS_FileClose(in);
	ok &= fclose(out) == 0;
	if(!ok && newFile) unlink(b);
	return ok;
}

int OS_DirCreate(const char* path)
{
	char p[PATH_MAX];
	if(OsPosix_HasDataDirectory() && OS_FileAttrs(path) != OS_INVALID_ATTRS) return 0;
	if(!OsPosix_WritePath(path, p, sizeof p, 1)) return 0;
	return mkdir(p, 0755) == 0;
}

int OS_DirRemove(const char* path)
{
	char p[PATH_MAX];
	if(!OsPosix_WritePath(path, p, sizeof p, 0)) return 0;
	return rmdir(p) == 0;
}

// ---- directory enumeration ---------------------------------------------------------------------

struct OsFind
{
	DIR* dirs[3];           // patch, private data, original directory
	char paths[3][PATH_MAX];
	char pattern[260];      // UTF-8 file part with wildcards
	int archivePattern;    // enumerate the selected logical archive once per name
	uint32_t discIndex;
};

// the next entry that matches the pattern into `out` (attributes from stat, OS_ATTR_NORMAL when stat fails); 0 at the end
static int FindFillDir(OsFind_t* h, int layer, OsFindData_t* out)
{
	DIR* dir = h->dirs[layer];
	const char* dirPath = h->paths[layer];
	struct dirent* e;
	while(dir && (e = readdir(dir)) != NULL)
	{
		char full[PATH_MAX], logical[260];
		struct stat st;
		const char* name = e->d_name;
		if(layer != 2 && (!strcmp(name, ".") || !strcmp(name, "..") || !strcasecmp(name, ".openbgi")))
			continue;
		if(h->archivePattern)
		{
			OsPosix_ArchiveLogicalName(name, logical, sizeof logical);
			name = logical;
		}
		if(!OsCommon_WildcardMatch(h->pattern, name))
			continue;
		snprintf(full, sizeof full, "%s/%s", dirPath, e->d_name);
		if(h->archivePattern)
		{
			char requested[PATH_MAX], selected[PATH_MAX];
			snprintf(requested, sizeof requested, "%s/%s", dirPath, name);
			if(strcasecmp(OsPosix_LocalizedArchive(requested, selected, sizeof selected), full))
				continue;
		}
		if(layer != 0)
		{
			char requested[PATH_MAX], selected[PATH_MAX];
			snprintf(requested, sizeof requested, "%s/%s", h->paths[2], name);
			if((h->dirs[0] && OsPosix_PatchPath(requested, selected, sizeof selected, h->archivePattern)) ||
				(layer == 2 && ((h->dirs[1] && OsPosix_DataPath(requested, selected, sizeof selected, h->archivePattern)) ||
					OsPosix_BaseHidden(requested))))
				continue;
		}
		out->attrs = OS_ATTR_NORMAL;
		if(stat(full, &st) == 0)
		{
			out->attrs = 0;
			if(S_ISDIR(st.st_mode))
				out->attrs |= OS_ATTR_DIRECTORY;
			if(!(st.st_mode & S_IWUSR))
				out->attrs |= OS_ATTR_READONLY;
			if(!out->attrs)
				out->attrs = OS_ATTR_NORMAL;
		}
		// the name goes back to the engine as it is stored (UTF-8 names that are plain ASCII survive unchanged; others are not converted)
		snprintf(out->name, sizeof out->name, "%s", name);
		return 1;
	}
	return 0;
}

static int FindFill(OsFind_t* h, OsFindData_t* out)
{
	if(FindFillDir(h, 0, out) || FindFillDir(h, 1, out) || FindFillDir(h, 2, out))
		return 1;
	const char* directory = OsPosix_DiscDirectory(h->paths[2]);
	const char* leaf;
	const VolEntry_t* entry;
	while((entry = Disc_Next(directory, &h->discIndex, &leaf)))
	{
		char name[sizeof out->name], native[PATH_MAX], layer[PATH_MAX];
		struct stat st;
		// A deflated ZIP entry is readable here too: Disc_Open inflates it.
		if(!entry->isDir && (!entry->readable || entry->size > UINT32_MAX))
			continue;
		if(h->archivePattern)
			OsPosix_ArchiveLogicalName(leaf, name, sizeof name);
		else
			snprintf(name, sizeof name, "%s", leaf);
		if(!OsCommon_WildcardMatch(h->pattern, name) ||
			snprintf(native, sizeof native, "%s/%s", h->paths[2], name) >= (int)sizeof native ||
			OsPosix_DiscPath(native, h->archivePattern) != entry)
			continue;
		const char* base = h->archivePattern ? OsPosix_LocalizedArchive(native, layer, sizeof layer) : native;
		if(!stat(base, &st) || OsPosix_PatchPath(native, layer, sizeof layer, h->archivePattern) ||
			OsPosix_DataPath(native, layer, sizeof layer, h->archivePattern))
			continue;
		snprintf(out->name, sizeof out->name, "%s", name);
		out->attrs = OS_ATTR_READONLY | (entry->isDir ? OS_ATTR_DIRECTORY : 0);
		return 1;
	}
	return 0;
}

/* open the pattern's directory (the current one without a directory
 * part) and deliver the first match; NULL when the directory cannot be
 * read or nothing matches */
OsFind_t* OS_FindFirst(const char* pattern, OsFindData_t* out)
{
	OsFind_t* h = (OsFind_t*)calloc(1, sizeof *h);
	char win[PATH_MAX], utfPat[260];
	const char* file;
	size_t dirLen;
	if(!h) return NULL;

	// split the Windows pattern into directory and file part before converting
	if(OsPosix_Utf8Paths())
	{
		file = pattern;
		for(const char* p = pattern; *p; ++p)
			if(*p == '/' || *p == '\\' || *p == ':')
				file = p + 1;
	}
	else
		file = OsCommon_FilePart(pattern);
	dirLen = (size_t)(file - pattern);
	if(dirLen >= sizeof win)
		dirLen = sizeof win - 1;
	memcpy(win, pattern, dirLen);
	win[dirLen] = 0;
	OsPosix_Path(dirLen == 0 ? "." : win, h->paths[2], sizeof h->paths[2]);
	OsPosix_PathToUtf8(file, utfPat, sizeof utfPat);
	snprintf(h->pattern, sizeof h->pattern, "%s", utfPat);
	size_t patternLen = strlen(h->pattern);
	char logical[sizeof h->pattern];
	OsPosix_ArchiveLogicalName(h->pattern, logical, sizeof logical);
	h->archivePattern = patternLen >= 4 && !strcasecmp(h->pattern + patternLen - 4, ".arc") &&
		!strcmp(h->pattern, logical);
	h->dirs[2] = OsPosix_BaseHidden(h->paths[2]) ? NULL : opendir(h->paths[2]);
	if(OsPosix_PatchPath(h->paths[2], h->paths[0], sizeof h->paths[0], 0))
		h->dirs[0] = opendir(h->paths[0]);
	if(OsPosix_DataPath(h->paths[2], h->paths[1], sizeof h->paths[1], 0))
		h->dirs[1] = opendir(h->paths[1]);
	if(!FindFill(h, out))
	{
		OS_FindClose(h);
		return NULL;
	}
	return h;
}

int OS_FindNext(OsFind_t* h, OsFindData_t* out)
{
	return FindFill(h, out);
}

void OS_FindClose(OsFind_t* h)
{
	if(!h)
		return;
	for(int i = 0; i < 3; ++i)
		if(h->dirs[i]) closedir(h->dirs[i]);
	free(h);
}

// ---- drives: one fixed pseudo drive ------------------------------------------------------------

// "C:" is the game directory and fixed; any other letter has no root (so the CD search finds nothing)
uint32_t OS_DriveType(const char* root)
{
	return (root[0] == 'C' || root[0] == 'c') ? OS_DRIVE_FIXED : OS_DRIVE_NO_ROOT;
}

// the list holds the one drive: "C:\" NUL NUL
uint32_t OS_LogicalDrives(char* buf, uint32_t n)
{
	static const char list[] = "C:\\\0";
	if(n < sizeof list + 1)
		return 0;
	memcpy(buf, list, sizeof list);
	buf[sizeof list] = 0;
	return sizeof list - 1; // "C:\" NUL
}

// the pseudo drive is always ready and always holds its medium
int OS_VolumeReady(const char* root)
{
	return OS_DriveType(root) == OS_DRIVE_FIXED;
}

int OS_MediaPresent(char driveLetter)
{
	return driveLetter == 'C' || driveLetter == 'c';
}
