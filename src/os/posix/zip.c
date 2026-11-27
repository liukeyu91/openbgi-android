/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * zip.c - a ZIP archive as a volume, for the POSIX disc reader: the format
 *         alone; not part of the original engine
 *
 * PKWARE's APPNOTE: the central directory at the end of the file, zip64 for
 * archives and entries past 4 GB, entries stored or deflated (an entry
 * compressed another way, or encrypted, is listed but cannot be read) and
 * checked against their CRC-32 when they are read.
 *
 * Zip_Read reads the end records and the central directory and hands the
 * list one entry per file, its name a clean path:
 *
 *   - names are UTF-8: as the entry says it stores them (flag bit 11, the
 *     Info-ZIP Unicode path field), else as found when they are well-formed
 *     UTF-8, else taken as Shift-JIS - what archives made on Japanese
 *     Windows hold;
 *   - '\' is a separator too, "." and empty components are dropped, a name
 *     that leaves the root ("..") is ignored, and so is everything under
 *     "__MACOSX/".
 *
 * A stored entry keeps its byte range in `data`, which the file services
 * read where it lies. A deflated one keeps the range of its compressed
 * bytes there and reports `stored` = 0: Zip_EntryBytes inflates it once,
 * when the engine opens it.
 */
#include "disc_internal.h"

#include "bgi/bytes.h"
#include "bgi/deflate.h"
#include "bgi/os.h"

#define SIG_LOCAL     0x04034b50u
#define SIG_CENTRAL   0x02014b50u
#define SIG_END       0x06054b50u
#define SIG_END64     0x06064b50u
#define SIG_LOCATOR64 0x07064b50u
#define END_SIZE      22     // the end of central directory record without its comment
#define COMMENT_MAX   0xffff // the longest archive comment

/* An inflated entry is held whole in memory while it is open. The engine
 * opens the game's scripts, archives and images one at a time; this bound
 * keeps a hostile or unusual archive from exhausting the device. */
#define ZIP_ENTRY_MAX (256u * 1024 * 1024)

// ---- names -----------------------------------------------------------------------------------------

// whether `n` bytes are well-formed UTF-8 (`ascii` is set to whether they are all ASCII)
static int IsUtf8(const uint8_t* s, size_t n, int* ascii)
{
	size_t i = 0;
	*ascii = 1;
	while(i < n)
	{
		uint8_t c = s[i];
		size_t len, k;
		uint32_t cp;
		if(c < 0x80)
		{
			i++;
			continue;
		}
		*ascii = 0;
		if(c >= 0xc2 && c <= 0xdf)
			len = 2, cp = c & 0x1f;
		else if(c >= 0xe0 && c <= 0xef)
			len = 3, cp = c & 0x0f;
		else if(c >= 0xf0 && c <= 0xf4)
			len = 4, cp = c & 0x07;
		else
			return 0;
		if(i + len > n)
			return 0;
		for(k = 1; k < len; k++)
		{
			if((s[i + k] & 0xc0) != 0x80)
				return 0;
			cp = (cp << 6) | (s[i + k] & 0x3f);
		}
		if((len == 3 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)))
			return 0;
		i += len;
	}
	return 1;
}

/* a stored name as UTF-8 into out (cap bytes): taken as it is when `utf8`
 * is set or the bytes are UTF-8, else decoded as Shift-JIS (whatever the
 * engine's text mode) */
static void DecodeName(const uint8_t* raw, size_t n, int utf8, char* out, size_t cap)
{
	char sjis[VOL_NAME_MAX];
	int ascii;
	size_t i;
	if(!utf8 && !IsUtf8(raw, n, &ascii))
	{
		for(i = 0; i < n && i + 1 < sizeof sjis; i++)
			sjis[i] = (char)raw[i];
		sjis[i] = 0;
		OS_SjisToUtf8(sjis, out, cap);
		return;
	}
	if(n >= cap)
		n = cap - 1;
	memcpy(out, raw, n);
	out[n] = 0;
}

/* a decoded name as a clean relative path in place: '\' becomes '/',
 * empty and "." components go, a trailing '/' is dropped (and reported in
 * *isDir); 0 when the name leaves the root or nothing is left */
static int CleanPath(char* s, int* isDir)
{
	char* w = s;
	const char* r = s;
	size_t len = strlen(s);
	*isDir = len && (s[len - 1] == '/' || s[len - 1] == '\\');
	while(*r)
	{
		const char* start;
		size_t n;
		while(*r == '/' || *r == '\\')
			r++;
		start = r;
		while(*r && *r != '/' && *r != '\\')
			r++;
		n = (size_t)(r - start);
		if(n == 0 || (n == 1 && start[0] == '.'))
			continue;
		if(n == 2 && start[0] == '.' && start[1] == '.')
			return 0;
		if(w != s)
			*w++ = '/';
		memmove(w, start, n);
		w += n;
	}
	*w = 0;
	return w != s;
}

// ---- the directory ---------------------------------------------------------------------------------

/* the end records of a file of `fileSize` bytes: the central directory's
 * offset, size and entry count; 0 when the file has no end record or spans
 * several disks */
static int FindDirectory(const VolIn_t* f, uint64_t fileSize, uint64_t* cdOff, uint64_t* cdSize, uint64_t* count)
{
	uint8_t* tail;
	uint64_t start;
	size_t n;
	long i;
	int found = 0;
	if(fileSize < END_SIZE)
		return 0;
	n = (size_t)(fileSize < END_SIZE + COMMENT_MAX ? fileSize : END_SIZE + COMMENT_MAX);
	start = fileSize - n;
	tail = (uint8_t*)malloc(n);
	if(!tail || !VolIn_ReadAt(f, start, tail, n))
	{
		free(tail);
		return 0;
	}
	for(i = (long)(n - END_SIZE); i >= 0; i--)
	{
		const uint8_t* e = tail + i;
		uint64_t at = start + (uint64_t)i;
		uint8_t loc[20], end64[56];
		if(Le32(e) != SIG_END)
			continue;
		if(Le16(e + 4) != 0 || Le16(e + 6) != 0 || Le16(e + 8) != Le16(e + 10))
			break; // several disks
		*count = Le16(e + 10);
		*cdSize = Le32(e + 12);
		*cdOff = Le32(e + 16);
		found = 1;
		// a zip64 locator right before the end record points to the zip64 end record, which has the full values
		if(at >= 20 && VolIn_ReadAt(f, at - 20, loc, 20) && Le32(loc) == SIG_LOCATOR64 && VolIn_ReadAt(f, Le64(loc + 8), end64, 56) &&
			Le32(end64) == SIG_END64)
		{
			if(Le32(end64 + 16) != 0 || Le32(end64 + 20) != 0)
				found = 0; // several disks
			*count = Le64(end64 + 32);
			*cdSize = Le64(end64 + 40);
			*cdOff = Le64(end64 + 48);
		}
		break;
	}
	free(tail);
	return found;
}

/* the extra fields of a central directory record: the zip64 sizes and
 * offset where the record's own fields are saturated, the time of an NTFS
 * or an extended timestamp field, and the UTF-8 name of an Info-ZIP
 * Unicode path field whose check value matches the stored name (copied to
 * `uname`, which stays "" otherwise) */
static void ParseExtra(const uint8_t* x, size_t n, const uint8_t* rawName, size_t rawLen, VolEntry_t* e, int zipU, int zipC,
	int zipL, char* uname, size_t ucap)
{
	size_t i = 0;
	uname[0] = 0;
	while(i + 4 <= n)
	{
		uint32_t id = Le16(x + i), len = Le16(x + i + 2);
		const uint8_t* d = x + i + 4;
		if(i + 4 + len > n)
			break;
		if(id == 0x0001)
		{ // zip64: only the fields whose 32-bit form is saturated, in this order
			size_t k = 0;
			if(zipU && k + 8 <= len)
				e->size = Le64(d + k), k += 8;
			if(zipC && k + 8 <= len)
				e->csize = Le64(d + k), k += 8;
			if(zipL && k + 8 <= len)
				e->data = Le64(d + k), k += 8;
		}
		else if(id == 0x000a && len >= 32 && Le16(d + 4) == 1 && Le16(d + 6) >= 24)
			e->mtime = Le64(d + 8); // NTFS: the first attribute's modification FILETIME
		else if(id == 0x5455 && len >= 5 && (d[0] & 1) && !e->mtime)
			e->mtime = ((uint64_t)Le32(d + 1) + 11644473600ull) * 10000000ull; // Unix seconds (taken as unsigned: past 2038)
		else if(id == 0x7075 && len >= 5 && d[0] == 1 && Le32(d + 1) == BGI_Crc32(0, rawName, rawLen))
		{
			size_t m = len - 5 < ucap - 1 ? len - 5 : ucap - 1;
			int ascii;
			if(IsUtf8(d + 5, len - 5, &ascii))
			{
				memcpy(uname, d + 5, m);
				uname[m] = 0;
			}
		}
		i += 4 + len;
	}
}

/* Where the entry's bytes start and whether they can be read: the local
 * header's name and extra fields may differ from the central directory's,
 * so the data offset is found there. A stored entry's `data` becomes that
 * offset; a deflated one keeps where its compressed bytes start. */
static void ResolveEntry(const VolIn_t* f, VolEntry_t* e, uint64_t fileSize)
{
	uint8_t h[30];
	uint64_t local = e->data, off;
	if(!e->readable || e->isDir)
		return;
	if(!VolIn_ReadAt(f, local, h, sizeof h) || Le32(h) != SIG_LOCAL)
	{
		e->readable = 0;
		return;
	}
	off = local + 30 + Le16(h + 26) + Le16(h + 28);
	if(off > fileSize || e->csize > fileSize - off || (e->method == 0 && e->size != e->csize))
	{
		e->readable = 0;
		return;
	}
	e->data = off;
}

// read the central directory into `l`; 0 when it is damaged
static int ReadDirectory(const VolIn_t* f, uint64_t fileSize, uint64_t cdOff, uint64_t cdSize, uint64_t count, VolList_t* l)
{
	uint8_t* cd;
	size_t p = 0;
	uint64_t i;
	if(count > VOL_MAX_ENTRIES || cdSize > (uint64_t)count * (46 + 3 * 0xffff) || cdSize > 0x40000000u)
		return 0;
	cd = (uint8_t*)malloc((size_t)cdSize + 1);
	if(!cd || !VolIn_ReadAt(f, cdOff, cd, (size_t)cdSize))
	{
		free(cd);
		return 0;
	}
	for(i = 0; i < count; i++)
	{
		const uint8_t* h = cd + p;
		uint32_t nameLen, extraLen, commentLen, flags;
		char name[VOL_NAME_MAX], uname[VOL_NAME_MAX];
		VolEntry_t e;
		int isDir;
		if(p + 46 > cdSize || Le32(h) != SIG_CENTRAL)
			break;
		nameLen = Le16(h + 28);
		extraLen = Le16(h + 30);
		commentLen = Le16(h + 32);
		if(p + 46 + nameLen + extraLen + commentLen > cdSize)
			break;
		memset(&e, 0, sizeof e);
		flags = Le16(h + 8);
		e.method = (uint8_t)Le16(h + 10);
		e.check = Le32(h + 16);
		e.csize = Le32(h + 20);
		e.size = Le32(h + 24);
		e.data = Le32(h + 42);
		ParseExtra(h + 46 + nameLen, extraLen, h + 46, nameLen, &e, e.size == 0xffffffffu, e.csize == 0xffffffffu,
			e.data == 0xffffffffu, uname, sizeof uname);
		if(uname[0])
			snprintf(name, sizeof name, "%s", uname);
		else
			DecodeName(h + 46, nameLen, (flags & 0x0800) != 0, name, sizeof name);
		e.encrypted = (flags & 1) != 0;
		e.readable = !e.encrypted && (e.method == 0 || e.method == 8);
		p += 46 + nameLen + extraLen + commentLen;
		if(!CleanPath(name, &isDir))
			continue;
		if(strncmp(name, "__MACOSX", 8) == 0 && (name[8] == 0 || name[8] == '/'))
			continue; // the resource forks of macOS's archiver
		e.isDir = (uint8_t)isDir;
		if(isDir)
			e.readable = 0;
		ResolveEntry(f, &e, fileSize);
		e.stored = e.readable && e.method == 0;
		if(!VolList_Add(l, name, &e))
		{
			free(cd);
			return 0;
		}
	}
	free(cd);
	return 1;
}

// ---- the parent directories -------------------------------------------------------------------------

/* An archive need not list the directories its files live in (many writers
 * only store file entries). The engine asks the volume for a directory's
 * attributes, so a missing parent is added as a directory entry. */
typedef struct NameSet
{
	char** slots;
	uint32_t capacity, count;
} NameSet_t;

static uint32_t HashName(const char* s)
{
	uint32_t h = 2166136261u;
	while(*s)
	{
		h ^= (uint8_t)*s++;
		h *= 16777619u;
	}
	return h;
}

static void SetFree(NameSet_t* set)
{
	for(uint32_t i = 0; i < set->capacity; i++)
		free(set->slots[i]);
	free(set->slots);
	memset(set, 0, sizeof *set);
}

static int SetGrow(NameSet_t* set)
{
	uint32_t capacity = set->capacity ? set->capacity * 2 : 256;
	char** slots = (char**)calloc(capacity, sizeof *slots);
	if(!slots)
		return 0;
	for(uint32_t i = 0; i < set->capacity; i++)
		if(set->slots[i])
		{
			uint32_t at = HashName(set->slots[i]) & (capacity - 1);
			while(slots[at])
				at = (at + 1) & (capacity - 1);
			slots[at] = set->slots[i];
		}
	free(set->slots);
	set->slots = slots;
	set->capacity = capacity;
	return 1;
}

// 1 when the name was added, 0 when it was there already, -1 when memory ran out
static int SetAdd(NameSet_t* set, const char* name)
{
	uint32_t at;
	if(set->count * 4 >= set->capacity * 3 && !SetGrow(set))
		return -1;
	at = HashName(name) & (set->capacity - 1);
	while(set->slots[at])
	{
		if(!strcmp(set->slots[at], name))
			return 0;
		at = (at + 1) & (set->capacity - 1);
	}
	set->slots[at] = strdup(name);
	if(!set->slots[at])
		return -1;
	set->count++;
	return 1;
}

static int AddParents(VolList_t* list)
{
	NameSet_t set = {0};
	uint32_t count = VolList_Count(list), i;
	for(i = 0; i < count; i++)
	{
		const VolEntry_t* entry = VolList_At(list, i);
		if(entry && entry->path && SetAdd(&set, entry->path) < 0)
			goto fail;
	}
	for(i = 0; i < count; i++)
	{
		const VolEntry_t* entry = VolList_At(list, i);
		char path[VOL_NAME_MAX];
		if(!entry || !entry->path || entry->isDir || strlen(entry->path) >= sizeof path)
			continue;
		strcpy(path, entry->path);
		for(char* slash = strrchr(path, '/'); slash; slash = strrchr(path, '/'))
		{
			int added;
			VolEntry_t dir;
			*slash = 0;
			added = SetAdd(&set, path);
			if(added < 0)
				goto fail;
			if(!added)
				continue;
			memset(&dir, 0, sizeof dir);
			dir.isDir = 1;
			if(!VolList_Add(list, path, &dir))
				goto fail;
		}
	}
	SetFree(&set);
	return 1;
fail:
	SetFree(&set);
	return 0;
}

int Zip_Read(const VolIn_t* f, uint64_t fileSize, VolList_t* l){
	uint64_t cdOff = 0, cdSize = 0, count = 0;
	if(!FindDirectory(f, fileSize, &cdOff, &cdSize, &count))
		return 0; // not a zip archive
	if(!ReadDirectory(f, fileSize, cdOff, cdSize, count, l))
		return -1;
	return AddParents(l) ? 1 : -1;
}

// ---- the bytes of an entry -------------------------------------------------------------------------

/* A positioned read for run time: the engine may open entries while another
 * thread reads a stored one, so this leaves the stream's position alone
 * (VolIn_ReadAt seeks it, which is fine only while the index is read). */
static int ReadExactly(const VolIn_t* f, uint64_t offset, void* buf, size_t size)
{
	int fd = fileno((FILE*)f);
	size_t done = 0;
	if(offset > (uint64_t)INT64_MAX)
		return 0;
	while(done < size)
	{
		ssize_t got = pread(fd, (uint8_t*)buf + done, size - done, (off_t)(offset + done));
		if(got < 0 && errno == EINTR)
			continue;
		if(got <= 0)
			return 0;
		done += (size_t)got;
	}
	return 1;
}

uint8_t* Zip_EntryBytes(const VolIn_t* f, const VolEntry_t* e, size_t* size)
{
	uint8_t* raw;
	uint8_t* out;
	size_t n = 0;
	if(!f || !e || !e->readable || e->isDir || e->method != 8 || e->csize == 0)
		return NULL;
	if(e->size > ZIP_ENTRY_MAX || e->csize > ZIP_ENTRY_MAX || e->size > (uint64_t)SIZE_MAX - 1)
		return NULL;
	raw = (uint8_t*)malloc((size_t)e->csize);
	if(!raw)
		return NULL;
	if(!ReadExactly(f, e->data, raw, (size_t)e->csize))
	{
		free(raw);
		return NULL;
	}
	out = Deflate_InflateRaw(raw, (size_t)e->csize, (size_t)e->size + 1, &n);
	free(raw);
	if(!out || n != e->size || BGI_Crc32(0, out, n) != e->check)
	{
		free(out);
		return NULL;
	}
	out[n] = 0;
	*size = n;
	return out;
}
