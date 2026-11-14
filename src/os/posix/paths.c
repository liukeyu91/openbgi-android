/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * paths.c - Windows paths on a POSIX file system (inc/bgi/os_posix.h)
 *
 * Paths arrive as the engine builds them (Shift-JIS, '\\' separators,
 * optionally a "C:\" drive).  OsPosix_Path() turns them into UTF-8 POSIX
 * paths under the game directory, and when a path does not exist as
 * spelled it looks for a case-insensitive match component by component,
 * because the game data was written for a case-insensitive file system.
 * Every file service of files.c goes through it, so the engine never
 * sees a POSIX path.
 */
#include "posix_internal.h"

/* Missing loose-file probes precede archive reads. Scanning the same encrypted
 * Android directory for every probe is expensive. Keep four bounded snapshots,
 * validated on every use. Recent directories stay uncached to avoid stale
 * results when multiple mutations share a filesystem timestamp quantum. */
#define CASE_CACHE_COUNT 4
#define CASE_CACHE_BYTES (256u * 1024u)
typedef struct CaseDirectory
{
	char path[PATH_MAX];
	struct stat stamp;
	char* names;
	size_t bytes;
	uint64_t used;
} CaseDirectory_t;
static CaseDirectory_t gCaseDirs[CASE_CACHE_COUNT];
static pthread_mutex_t gCaseLock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t gCaseUse;

static int SameDirectory(const struct stat* a, const struct stat* b)
{
	return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
		a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
		a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

static void MatchCaseCached(const char* dir, char* name)
{
	struct stat before, after;
	CaseDirectory_t* slot = NULL;
	char* names = NULL;
	size_t bytes = 0, capacity = 0;
	int cacheable, readError;
	DIR* d;
	struct dirent* e;
	char found[NAME_MAX + 1] = {0};
	time_t now = time(NULL);
	if(stat(dir, &before) != 0) return;
	cacheable = strlen(dir) < PATH_MAX && now > before.st_mtime && now - before.st_mtime > 1 &&
		now > before.st_ctime && now - before.st_ctime > 1;
	pthread_mutex_lock(&gCaseLock);
	for(unsigned i = 0; i < CASE_CACHE_COUNT; ++i)
	{
		CaseDirectory_t* item = &gCaseDirs[i];
		if(!strcmp(item->path, dir) && item->names)
		{
			if(cacheable && SameDirectory(&item->stamp, &before))
			{
				for(size_t at = 0; at < item->bytes; at += strlen(item->names + at) + 1)
					if(!strcasecmp(item->names + at, name)) { strcpy(name, item->names + at); break; }
				item->used = ++gCaseUse;
				pthread_mutex_unlock(&gCaseLock);
				return;
			}
			free(item->names); memset(item, 0, sizeof *item);
		}
		if(!slot || item->used < slot->used) slot = item;
	}
	d = opendir(dir);
	if(!d) { pthread_mutex_unlock(&gCaseLock); return; }
	if(cacheable) { free(slot->names); memset(slot, 0, sizeof *slot); }
	errno = 0;
	while((e = readdir(d)) != NULL)
	{
		size_t n = strlen(e->d_name) + 1;
		if(!found[0] && !strcasecmp(e->d_name, name)) strcpy(found, e->d_name);
		if(cacheable)
		{
			if(bytes + n > CASE_CACHE_BYTES) cacheable = 0;
			else if(bytes + n > capacity)
			{
				size_t next = capacity ? capacity * 2 : 4096;
				if(next > CASE_CACHE_BYTES) next = CASE_CACHE_BYTES;
				char* grown = (char*)realloc(names, next);
				if(!grown) cacheable = 0;
				else { names = grown; capacity = next; }
			}
			if(cacheable) { memcpy(names + bytes, e->d_name, n); bytes += n; }
		}
		if(!cacheable && found[0]) break;
		errno = 0;
	}
	readError = errno;
	if(cacheable && !readError && fstat(dirfd(d), &after) == 0 && SameDirectory(&before, &after))
	{
		strcpy(slot->path, dir); slot->stamp = before;
		slot->names = names; slot->bytes = bytes; slot->used = ++gCaseUse;
		names = NULL;
	}
	free(names); closedir(d);
	if(found[0]) strcpy(name, found);
	pthread_mutex_unlock(&gCaseLock);
}

/* one entry of `dir` ("" = the current directory) that matches `name`
 * without regard to ASCII case; `name` is replaced in place by the entry's
 * spelling when a match exists (the first one readdir yields) */
static void MatchCase(const char* dir, char* name)
{
	MatchCaseCached(dir[0] ? dir : ".", name);
}

/* Shift-JIS Windows path -> UTF-8 POSIX path (into out, n bytes).  A drive
 * letter (any letter) maps to the executable's directory ("C:\" is where
 * the game lives), a leading separator to the file system root; '\\'
 * becomes '/'; every component that does not exist as spelled is replaced
 * by a case-insensitive match when there is one.  A relative path stays
 * relative (to the current directory); an empty one becomes "." or "/". */
void OsPosix_Path(const char* win, char* out, size_t n)
{
	char utf[PATH_MAX * 2];
	char cur[PATH_MAX]; // the POSIX path built so far
	const char* p;
	size_t o = 0;
	int absolute = 0;

	OsPosix_SjisToUtf8(win, utf, sizeof utf);
	p = utf;
	if(((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
	{
		p += 2;
		if(*p == '\\' || *p == '/')
			p++;
		snprintf(cur, sizeof cur, "%s", gPosixExeDir); // the drive root is the game directory
		o = strlen(cur);
		if(o && cur[o - 1] == '/')
			cur[--o] = 0;
		absolute = 1;
	}
	else if(*p == '\\' || *p == '/')
	{
		cur[0] = 0;
		absolute = 1;
		p++;
	}
	else
		cur[0] = 0;
	BGI_UNUSED(absolute);

	while(*p)
	{ // one component at a time: probe it as spelled, then look for another spelling
		char comp[PATH_MAX];
		size_t c = 0;
		struct stat st;
		while(*p && *p != '\\' && *p != '/' && c + 1 < sizeof comp)
			comp[c++] = *p++;
		comp[c] = 0;
		while(*p == '\\' || *p == '/')
			p++;
		if(c == 0)
			continue;
		if(strcmp(comp, ".") != 0 && strcmp(comp, "..") != 0)
		{
			char probe[PATH_MAX];
			const char* dir = cur[0] ? cur : absolute ? "/"
													  : ".";
			snprintf(probe, sizeof probe, "%s%s%s", cur, cur[0] || absolute ? "/" : "", comp);
			if(stat(probe, &st) != 0)
				MatchCase(dir, comp);
		}
		snprintf(cur + strlen(cur), sizeof cur - strlen(cur), "%s%s", cur[0] || absolute ? "/" : "", comp);
	}
	if(!cur[0])
		strcpy(cur, absolute ? "/" : ".");
	snprintf(out, n, "%s", cur);
}
