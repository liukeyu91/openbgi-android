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
#include "disc_internal.h"

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

/* Each localized distribution looks for its alternate and primary
 * archive names before the shared file. The traditional entry also
 * falls back to simplified resources. */
static const char* const kArchiveSuffixes[][5] = {
	{"c2", "cn", NULL},
	{"t2", "tc", "c2", "cn", NULL},
	{"e2", "en", NULL},
};
static const char* const* gArchiveSuffixes;

int OsPosix_SetArchiveLanguage(const char* language)
{
	if(!language[0])
		gArchiveSuffixes = NULL;
	else if(!strcmp(language, "cn"))
		gArchiveSuffixes = kArchiveSuffixes[0];
	else if(!strcmp(language, "tc"))
		gArchiveSuffixes = kArchiveSuffixes[1];
	else if(!strcmp(language, "en"))
		gArchiveSuffixes = kArchiveSuffixes[2];
	else
		return 0;
	return 1;
}

static int ArchiveHasLanguage(const char* name, size_t len)
{
	if(len < 8 || name[len - 7] != '.' || strcasecmp(name + len - 4, ".arc"))
		return 0;
	for(size_t i = 0; i < BGI_COUNTOF(kArchiveSuffixes); ++i)
		for(size_t j = 0; j < 2; ++j)
			if(!strncasecmp(name + len - 6, kArchiveSuffixes[i][j], 2))
				return 1;
	return 0;
}

void OsPosix_ArchiveLogicalName(const char* name, char* out, size_t n)
{
	size_t len = strlen(name);
	if(gArchiveSuffixes && ArchiveHasLanguage(name, len))
		snprintf(out, n, "%.*s.arc", (int)(len - 7), name);
	else
		snprintf(out, n, "%s", name);
}

/* The input is a native path already converted and case-corrected by
 * OsPosix_Path. Keep the spelling of a localized file found on disk. */
const char* OsPosix_LocalizedArchive(const char* path, char* out, size_t n)
{
	size_t len = strlen(path);
	const char* part = strrchr(path, '/');
	size_t dirLen = part ? (size_t)(part - path) : 0;
	struct stat st;
	if(!gArchiveSuffixes || len < 5 || len + 3 >= n || strcasecmp(path + len - 4, ".arc") ||
		ArchiveHasLanguage(part ? part + 1 : path, len - (part ? dirLen + 1 : 0)))
		return path;
	for(const char* const* suffix = gArchiveSuffixes; *suffix; ++suffix)
	{
		snprintf(out, n, "%.*s.%s.arc", (int)(len - 4), path, *suffix);
		if(stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
			return out;
		char dir[PATH_MAX];
		if(!part)
			strcpy(dir, ".");
		else
			snprintf(dir, sizeof dir, "%.*s", (int)(dirLen ? dirLen : 1), path);
		MatchCase(dir, out + (part ? dirLen + 1 : 0));
		if(stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
			return out;
	}
	return path;
}

static void AppendComponents(const char* p, char* cur, size_t size, int absolute)
{
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
		snprintf(cur + strlen(cur), size - strlen(cur), "%s%s", cur[0] || absolute ? "/" : "", comp);
	}
}

static char gPatchDirectory[PATH_MAX];
static char gDataDirectory[PATH_MAX];
static char** gOriginalSaves;
static size_t gOriginalSaveCount;

static int CompareSaves(const void* first, const void* second)
{
	return strcasecmp(*(const char* const*)first, *(const char* const*)second);
}

int OsPosix_SetDataDirectory(const char* native)
{
	char path[PATH_MAX], line[PATH_MAX];
	struct stat st;
	FILE* index;
	for(size_t i = 0; i < gOriginalSaveCount; ++i) free(gOriginalSaves[i]);
	free(gOriginalSaves);
	gOriginalSaves = NULL;
	gOriginalSaveCount = 0;
	gDataDirectory[0] = 0;
	if(!*native) return 1;
	if(*native != '/' || stat(native, &st) || !S_ISDIR(st.st_mode) ||
		snprintf(path, sizeof path, "%s/.openbgi/original-saves.txt", native) >= (int)sizeof path)
		return 0;
	index = fopen(path, "rb");
	if(!index) return 0;
	size_t capacity = 0;
	int ok = 1;
	while(fgets(line, sizeof line, index))
	{
		char* end = strchr(line, '\n');
		if(!end || end == line) { ok = 0; break; }
		*end = 0;
		if(gOriginalSaveCount == capacity)
		{
			capacity = capacity ? 2 * capacity : 32;
			char** grown = realloc(gOriginalSaves, capacity * sizeof *grown);
			if(!grown) { ok = 0; break; }
			gOriginalSaves = grown;
		}
		char* name = strdup(line);
		if(!name) { ok = 0; break; }
		gOriginalSaves[gOriginalSaveCount++] = name;
	}
	ok &= !ferror(index);
	fclose(index);
	if(!ok) return 0;
	if(gOriginalSaveCount)
		qsort(gOriginalSaves, gOriginalSaveCount, sizeof *gOriginalSaves, CompareSaves);
	strcpy(gDataDirectory, native);
	return 1;
}

int OsPosix_HasDataDirectory(void) { return *gDataDirectory != 0; }

static const char* RelativeTo(const char* path, const char* root)
{
	size_t len = strlen(root);
	while(len && root[len - 1] == '/') --len;
	if(strncmp(path, root, len) || (path[len] && path[len] != '/')) return NULL;
	path += len;
	while(*path == '/') ++path;
	return path;
}

static const char* GameRelative(const char* native)
{
	const char* p = *native == '/' ? RelativeTo(native, gPosixExeDir) : native;
	if(!p) return NULL;
	while(!strncmp(p, "./", 2)) p += 2;
	if(!strncasecmp(p, ".openbgi", 8) && (!p[8] || p[8] == '/')) return NULL;
	for(const char* part = p; *part;)
	{
		size_t len = strcspn(part, "/");
		if(len == 2 && part[0] == '.' && part[1] == '.') return NULL;
		part += len;
		while(*part == '/') ++part;
	}
	return p;
}

int OsPosix_BaseHidden(const char* native)
{
	if(!*gDataDirectory) return 0;
	const char* p = *native == '/' ? RelativeTo(native, gPosixExeDir) : native;
	if(!p) return 0;
	if(!strncasecmp(p, ".openbgi", 8) && (!p[8] || p[8] == '/')) return 1;
	return gOriginalSaveCount && bsearch(&p, gOriginalSaves, gOriginalSaveCount,
		sizeof *gOriginalSaves, CompareSaves) != NULL;
}

const char* OsPosix_DiscDirectory(const char* native)
{
	if(!Disc_Mounted())
		return NULL;
	const char* relative = GameRelative(native);
	return relative && !strcmp(relative, ".") ? "" : relative;
}

const VolEntry_t* OsPosix_DiscPath(const char* native, int localized)
{
	if(!Disc_Mounted() || !native[0] || OsPosix_BaseHidden(native))
		return NULL;
	const char* relative = GameRelative(native);
	const VolEntry_t* entry = Disc_Find(relative);
	if(!relative || !localized || !gArchiveSuffixes)
		return entry;
	size_t len = strlen(relative);
	if(len < 5 || strcasecmp(relative + len - 4, ".arc") ||
		ArchiveHasLanguage(OsCommon_FilePart(relative), strlen(OsCommon_FilePart(relative))))
		return entry;
	for(const char* const* suffix = gArchiveSuffixes; *suffix; suffix++)
	{
		char path[VOL_NAME_MAX];
		if(snprintf(path, sizeof path, "%.*s.%s.arc", (int)(len - 4), relative, *suffix) >= (int)sizeof path)
			break;
		const VolEntry_t* localizedEntry = Disc_Find(path);
		if(localizedEntry && !localizedEntry->isDir && localizedEntry->readable && localizedEntry->size)
			return localizedEntry;
	}
	return entry;
}

int OsPosix_SetPatchDirectory(const char* native)
{
	struct stat st;
	gPatchDirectory[0] = 0;
	if(!native[0])
		return 1;
	if(native[0] != '/' || strlen(native) >= sizeof gPatchDirectory ||
		stat(native, &st) || !S_ISDIR(st.st_mode))
		return 0;
	strcpy(gPatchDirectory, native);
	return 1;
}

/* The file services pass a native path within the game's virtual C: drive. */
static int LayerPath(const char* directory, const char* native, char* out, size_t n, int localize)
{
	char path[PATH_MAX];
	const char* p = GameRelative(native);
	struct stat st;
	if(!*directory || !p || strlen(directory) + strlen(p) + 2 > sizeof path)
		return 0;
	strcpy(path, directory);
	if(*p && strcmp(p, "."))
		AppendComponents(p, path, sizeof path, 1);
	if(localize && OsPosix_LocalizedArchive(path, out, n) == out)
		return 1;
	if(stat(path, &st) != 0 || strlen(path) >= n)
		return 0;
	strcpy(out, path);
	return 1;
}

int OsPosix_PatchPath(const char* native, char* out, size_t n, int localize)
{
	return LayerPath(gPatchDirectory, native, out, n, localize);
}

int OsPosix_DataPath(const char* native, char* out, size_t n, int localize)
{
	return LayerPath(gDataDirectory, native, out, n, localize);
}

void OsPosix_ReadPath(const char* win, char* out, size_t n)
{
	char native[PATH_MAX];
	OsPosix_Path(win, native, sizeof native);
	if(OsPosix_PatchPath(native, out, n, 1) || OsPosix_DataPath(native, out, n, 1))
		return;
	if(OsPosix_BaseHidden(native)) { if(n) *out = 0; return; }
	if(OsPosix_LocalizedArchive(native, out, n) != out)
		snprintf(out, n, "%s", native);
}

int OsPosix_WritePath(const char* win, char* out, size_t n, int createParents)
{
	char native[PATH_MAX], path[PATH_MAX];
	OsPosix_Path(win, native, sizeof native);
	if(!*gDataDirectory)
		return snprintf(out, n, "%s", native) < (int)n;
	const char* relative = GameRelative(native);
	if(!relative || !*relative || strlen(gDataDirectory) + strlen(relative) + 2 > sizeof path)
		return 0;
	strcpy(path, gDataDirectory);
	size_t root = strlen(path);
	AppendComponents(relative, path, sizeof path, 1);
	if(strlen(path) >= n) return 0;
	if(createParents)
	{
		char* last = strrchr(path, '/');
		char baseParent[PATH_MAX], overlayParent[PATH_MAX];
		struct stat st;
		if(last > path + root)
		{
			strcpy(baseParent, native);
			*strrchr(baseParent, '/') = 0;
			const char* parent = baseParent;
			if(OsPosix_PatchPath(baseParent, overlayParent, sizeof overlayParent, 0) ||
				OsPosix_DataPath(baseParent, overlayParent, sizeof overlayParent, 0))
				parent = overlayParent;
			if(stat(parent, &st))
			{
				const VolEntry_t* entry = OsPosix_DiscPath(baseParent, 0);
				if(!entry || !entry->isDir) return 0;
			}
			else if(!S_ISDIR(st.st_mode)) return 0;
			for(char* part = path + root + 1; (part = strchr(part, '/')) != NULL; ++part)
			{
				*part = 0;
				int made = !mkdir(path, 0700) || errno == EEXIST;
				*part = '/';
				if(!made) return 0;
			}
		}
	}
	strcpy(out, path);
	return 1;
}

static void NormalizePath(char* path)
{
	char* read = path;
	char* write = path;
	while(*read)
	{
		while(*read == '/') ++read;
		size_t len = strcspn(read, "/");
		if(len == 2 && !memcmp(read, "..", 2))
		{
			while(write > path && write[-1] != '/') --write;
			if(write > path) --write;
		}
		else if(len && (len != 1 || *read != '.'))
		{
			*write++ = '/';
			memmove(write, read, len);
			write += len;
		}
		read += len;
	}
	if(write == path) *write++ = '/';
	*write = 0;
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
	{
		cur[0] = 0;
		if(*gDataDirectory)
		{
			char cwd[PATH_MAX];
			if(!getcwd(cwd, sizeof cwd)) { if(n) *out = 0; return; }
			const char* relative = RelativeTo(cwd, gDataDirectory);
			if(!relative && *gPatchDirectory) relative = RelativeTo(cwd, gPatchDirectory);
			if(relative) snprintf(cur, sizeof cur, "%s/%s", gPosixExeDir, relative);
			else strcpy(cur, cwd);
			NormalizePath(cur);
			absolute = 1;
		}
	}

	AppendComponents(p, cur, sizeof cur, absolute);
	if(!cur[0])
		strcpy(cur, absolute ? "/" : ".");
	if(*gDataDirectory) NormalizePath(cur);
	snprintf(out, n, "%s", cur);
}
