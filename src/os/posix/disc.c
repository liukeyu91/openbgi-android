/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "disc_internal.h"

struct VolList
{
	VolEntry_t* entries;
	uint32_t count, capacity;
};

static VolList_t gDisc;
static FILE* gDiscFile;
static char gDiscRoot[VOL_NAME_MAX];
static char gDiscPath[PATH_MAX];	// the image file the mounted list came from

int VolIn_ReadAt(const VolIn_t* file, uint64_t offset, void* buf, size_t size)
{
	FILE* stream = (FILE*)file;
	return offset <= INT64_MAX && !fseeko(stream, (off_t)offset, SEEK_SET) && fread(buf, 1, size, stream) == size;
}

int VolList_Add(VolList_t* list, const char* path, const VolEntry_t* entry)
{
	if(list->count == VOL_MAX_ENTRIES)
		return 0;
	if(list->count == list->capacity)
	{
		uint32_t capacity = list->capacity ? list->capacity * 2 : 256;
		VolEntry_t* entries = realloc(list->entries, capacity * sizeof *entries);
		if(!entries)
			return 0;
		list->entries = entries;
		list->capacity = capacity;
	}
	char* copy = strdup(path);
	if(!copy)
		return 0;
	list->entries[list->count] = *entry;
	list->entries[list->count++].path = copy;
	return 1;
}

uint32_t VolList_Count(const VolList_t* list)
{
	return list->count;
}

VolEntry_t* VolList_At(VolList_t* list, uint32_t index)
{
	return index < list->count ? &list->entries[index] : NULL;
}

static void FreeEntries(VolList_t* list)
{
	for(uint32_t i = 0; i < list->count; i++)
		free(list->entries[i].path);
	free(list->entries);
	memset(list, 0, sizeof *list);
}

static int CompareEntries(const void* a, const void* b)
{
	return strcasecmp(((const VolEntry_t*)a)->path, ((const VolEntry_t*)b)->path);
}

static void FindGameRoot(const VolList_t* list, char* root)
{
	size_t bestDepth = VOL_NAME_MAX;
	root[0] = 0;
	for(uint32_t i = 0; i < list->count; i++)
	{
		const VolEntry_t* entry = &list->entries[i];
		const char* name = strrchr(entry->path, '/');
		name = name ? name + 1 : entry->path;
		size_t prefix = (size_t)(name - entry->path);
		if(entry->isDir || !entry->readable || !entry->size ||
			(strcasecmp(name, "ipl._bp") && strcasecmp(name, "system.arc")))
			continue;
		size_t depth = 0;
		for(size_t j = 0; j < prefix; j++)
			depth += entry->path[j] == '/';
		if(depth < bestDepth)
		{
			memcpy(root, entry->path, prefix);
			root[prefix] = 0;
			bestDepth = depth;
		}
		if(!depth)
			break;
	}
}

/* Mount a container read in place: an ISO 9660/UDF image or a ZIP archive.
 * The two formats are told apart by their first bytes (and an archive with
 * a header in front of its first record still answers the ISO probe with a
 * failure before the ZIP one is tried). */
int OsPosix_MountDisc(const char* path)
{
	FILE* file = NULL;
	VolList_t list = {0};
	char stored[PATH_MAX] = "";
	if(path && *path)
	{
		struct stat st;
		uint8_t magic[4];
		int zip = 0, ok;
		file = fopen(path, "rb");
		if(!file || fstat(fileno(file), &st) || !S_ISREG(st.st_mode) || st.st_size <= 0)
		{
			if(file)
				fclose(file);
			return 0;
		}
		if(VolIn_ReadAt(file, 0, magic, sizeof magic) && !memcmp(magic, "PK\3\4", 4))
			zip = 1;
		if(zip)
			ok = Zip_Read(file, (uint64_t)st.st_size, &list) > 0 && list.count;
		else
		{
			ok = Iso_Read(file, (uint64_t)st.st_size, &list) && list.count;
			if(!ok && !list.count)
				ok = Zip_Read(file, (uint64_t)st.st_size, &list) > 0 && list.count;
		}
		if(!ok || strlen(path) >= sizeof stored)
		{
			fclose(file);
			FreeEntries(&list);
			return 0;
		}
		strcpy(stored, path);
		qsort(list.entries, list.count, sizeof *list.entries, CompareEntries);
	}
	if(gDiscFile)
		fclose(gDiscFile);
	FreeEntries(&gDisc);
	gDisc = list;
	gDiscFile = file;
	strcpy(gDiscPath, stored);
	FindGameRoot(&gDisc, gDiscRoot);
	return 1;
}

/* Read-only views of the mounted index for the launcher's image probe: the
 * entries carry the image-relative path, the byte range and the same flags
 * the file services use. Disc_At walks the sorted list (the paths compare
 * case-insensitively, see CompareEntries). */
uint32_t Disc_Count(void)
{
	return gDiscFile ? gDisc.count : 0;
}

const VolEntry_t* Disc_At(uint32_t index)
{
	return gDiscFile && index < gDisc.count ? &gDisc.entries[index] : NULL;
}

const char* Disc_Root(void)
{
	return gDiscFile ? gDiscRoot : "";
}

const char* Disc_ImagePath(void)
{
	return gDiscFile ? gDiscPath : "";
}

int Disc_Dup(void)
{
	return gDiscFile ? fcntl(fileno(gDiscFile), F_DUPFD_CLOEXEC, 0) : -1;
}

int Disc_Mounted(void)
{
	return gDiscFile != NULL;
}

/* A read handle on one entry. A stored entry keeps a duplicated descriptor,
 * so it stays readable after the container is unmounted (as the file
 * services always allowed); a deflated ZIP entry is inflated once, here,
 * because its bytes are not a range of the file. */
int Disc_Open(const VolEntry_t* entry, DiscHandle_t* handle)
{
	memset(handle, 0, sizeof *handle);
	handle->fd = -1;
	if(!entry || entry->isDir || !entry->readable || entry->size > UINT32_MAX)
		return 0;
	if(entry->method == 8)
	{
		size_t size = 0;
		uint8_t* data = Zip_EntryBytes(gDiscFile, entry, &size);
		if(!data || size != entry->size)
		{
			free(data);
			return 0;
		}
		handle->data = data;
		handle->size = (uint32_t)size;
		return 1;
	}
	if(!entry->stored || !gDiscFile)
		return 0;
	handle->fd = Disc_Dup();
	if(handle->fd < 0)
		return 0;
	handle->offset = entry->data;
	handle->size = (uint32_t)entry->size;
	return 1;
}

uint32_t Disc_Read(DiscHandle_t* handle, void* buf, uint32_t n)
{
	uint32_t done = 0;
	if(!handle || (handle->fd < 0 && !handle->data))
		return 0;
	if(handle->position >= handle->size)
		return 0;
	n = BGI_MIN(n, handle->size - (uint32_t)handle->position);
	if(handle->data)
	{
		memcpy(buf, handle->data + handle->position, n);
		handle->position += n;
		return n;
	}
	while(done < n)
	{
		ssize_t bytes = pread(handle->fd, (uint8_t*)buf + done, n - done, (off_t)(handle->offset + handle->position));
		if(bytes < 0 && errno == EINTR)
			continue;
		if(bytes <= 0)
			break;
		done += (uint32_t)bytes;
		handle->position += (uint64_t)bytes;
	}
	return done;
}

void Disc_Close(DiscHandle_t* handle)
{
	if(!handle)
		return;
	if(handle->fd >= 0)
		close(handle->fd);
	free(handle->data);
	memset(handle, 0, sizeof *handle);
	handle->fd = -1;
}

static uint32_t LowerBound(const char* name)
{
	uint32_t begin = 0, end = gDisc.count;
	while(begin < end)
	{
		uint32_t mid = begin + (end - begin) / 2;
		if(strcasecmp(gDisc.entries[mid].path, name) < 0)
			begin = mid + 1;
		else
			end = mid;
	}
	return begin;
}

const VolEntry_t* Disc_Find(const char* relative)
{
	static const VolEntry_t root = {.isDir = 1};
	char path[VOL_NAME_MAX];
	if(!gDiscFile || !relative)
		return NULL;
	if(!relative[0] || !strcmp(relative, "."))
		return &root;
	if(snprintf(path, sizeof path, "%s%s", gDiscRoot, relative) >= (int)sizeof path)
		return NULL;
	uint32_t at = LowerBound(path);
	return at < gDisc.count && !strcasecmp(gDisc.entries[at].path, path) ? &gDisc.entries[at] : NULL;
}

const VolEntry_t* Disc_Next(const char* directory, uint32_t* index, const char** name)
{
	char prefix[VOL_NAME_MAX];
	if(!gDiscFile || !directory)
		return NULL;
	int len = snprintf(prefix, sizeof prefix, "%s%s%s", gDiscRoot, directory, directory[0] ? "/" : "");
	if(len < 0 || len >= (int)sizeof prefix)
		return NULL;
	if(!*index)
		*index = LowerBound(prefix);
	while(*index < gDisc.count)
	{
		const VolEntry_t* entry = &gDisc.entries[(*index)++];
		if(strncasecmp(entry->path, prefix, (size_t)len))
		{
			*index = gDisc.count;
			break;
		}
		const char* leaf = entry->path + len;
		if(*leaf && !strchr(leaf, '/'))
		{
			*name = leaf;
			return entry;
		}
	}
	return NULL;
}

size_t Vol_PutUtf8(char* out, size_t off, size_t cap, uint32_t cp)
{
	if(cp < 0x80)
	{
		if(off + 1 < cap)
			out[off++] = (char)cp;
	}
	else if(cp < 0x800)
	{
		if(off + 2 < cap)
		{
			out[off++] = (char)(0xc0 | (cp >> 6));
			out[off++] = (char)(0x80 | (cp & 0x3f));
		}
	}
	else if(cp < 0x10000)
	{
		if(off + 3 < cap)
		{
			out[off++] = (char)(0xe0 | (cp >> 12));
			out[off++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			out[off++] = (char)(0x80 | (cp & 0x3f));
		}
	}
	else if(off + 4 < cap)
	{
		out[off++] = (char)(0xf0 | (cp >> 18));
		out[off++] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[off++] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[off++] = (char)(0x80 | (cp & 0x3f));
	}
	return off;
}

int Vol_PutUtf16Be(const uint8_t* raw, size_t n, char* out, size_t cap)
{
	size_t off = 0, i;
	for(i = 0; i + 1 < n; i += 2)
	{
		uint32_t c = ((uint32_t)raw[i] << 8) | raw[i + 1];
		if(c >= 0xd800 && c < 0xdc00 && i + 3 < n)
		{
			uint32_t d = ((uint32_t)raw[i + 2] << 8) | raw[i + 3];
			if(d >= 0xdc00 && d < 0xe000)
			{
				c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
				i += 2;
			}
		}
		if(c < 0x20 || c == '/' || c == '\\')
			return 0;
		off = Vol_PutUtf8(out, off, cap, c);
	}
	out[off] = 0;
	return 1;
}

void Vol_SetTime(VolEntry_t* e, int year, int month, int day, int hour, int min, int sec, int zoneMin)
{
	int64_t days, secs;
	int y, m;
	if(month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
		return;

	if(year < 1601 || year > 9999)
		return;
	// the days since 1 January 1601 (the civil calendar counted from March)
	y = month <= 2 ? year - 1 : year;
	m = month <= 2 ? month + 9 : month - 3;
	days = (int64_t)365 * y + y / 4 - y / 100 + y / 400 + (153 * m + 2) / 5 + day - 1;
	days -= (int64_t)365 * 1600 + 1600 / 4 - 1600 / 100 + 1600 / 400 + (153 * 10 + 2) / 5; // 1 January 1601 is March-year 1600, month 10, day 1
	secs = days * 86400 + hour * 3600 + min * 60 + sec - (int64_t)zoneMin * 60;
	if(secs > 0)
		e->mtime = (uint64_t)secs * 10000000u;
}

