/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * arcread.c - the stand-alone archive reader of the tools; interface in
 *             arcread.h
 *
 * Reads a PackFile or BURIKO ARC20 index with stdio and hands out entries,
 * removing the DSC layer on request.  Independent of the engine's file
 * layer (file.h, the OS layer) so that bpasm and the extraction options can
 * link it on their own; only the DSC decoder of codec.h is shared.
 */
#include "bgi/arcread.h"
#include "bgi/os.h"
#include "bgi/codec.h"

// the two index magics (12 bytes each), repeated here so that file.h is not needed
#define PACKFILE_MAGIC "PackFile    "
#define ARC20_MAGIC    "BURIKO ARC20"

// open the archive at `path` with stdio and read its index (ArcRead_OpenFile); NULL with a message on stderr when the file cannot be opened
ArcRead_t* ArcRead_Open(const char* path)
{
	FILE* f = fopen(path, "rb");
	if(!f)
	{
		fprintf(stderr, "%s: cannot open\n", path);
		return NULL;
	}
	return ArcRead_OpenFile(f, path);
}

typedef size_t (*ArcIndexRead_t)(void* file, void* buffer, size_t size);

static size_t ReadStdio(void* file, void* buffer, size_t size)
{
	return fread(buffer, 1, size, file);
}

static size_t ReadGameFile(void* file, void* buffer, size_t size)
{
	return size <= UINT32_MAX ? OS_FileRead(file, buffer, (uint32_t)size) : 0;
}

/* Read the same index for tool inputs and the game's filesystem. */
static ArcRead_t* ReadIndex(void* file, ArcIndexRead_t read, const char* path)
{
	ArcRead_t* a;
	uint8_t hdr[16];
	uint32_t i, recSize, nameLen, base;
	uint8_t* raw;
	if(read(file, hdr, sizeof hdr) != sizeof hdr || (memcmp(hdr, PACKFILE_MAGIC, 12) != 0 && memcmp(hdr, ARC20_MAGIC, 12) != 0))
	{
		fprintf(stderr, "%s: not a PackFile / ARC20 archive\n", path);
		return NULL;
	}
	a = (ArcRead_t*)calloc(1, sizeof *a);
	if(!a)
		return NULL;
	a->arc20 = memcmp(hdr, ARC20_MAGIC, 12) == 0;
	a->count = hdr[12] | (hdr[13] << 8) | (hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
	recSize = a->arc20 ? 0x80 : 32; // record: name, offset, size (and padding)
	nameLen = a->arc20 ? 0x60 : 16;
	base = 16 + a->count * recSize; // the stored offsets count from the end of the index
	raw = (uint8_t*)malloc((size_t)a->count * recSize + 1);
	a->entries = (ArcEntry_t*)calloc(a->count + 1, sizeof *a->entries);
	if(!raw || !a->entries || read(file, raw, (size_t)a->count * recSize) != (size_t)a->count * recSize)
	{
		fprintf(stderr, "%s: truncated index\n", path);
		free(raw);
		ArcRead_Close(a);
		return NULL;
	}
	for(i = 0; i < a->count; i++)
	{
		const uint8_t* rec = raw + (size_t)i * recSize;
		uint32_t off, size;
		memcpy(a->entries[i].name, rec, nameLen);
		a->entries[i].name[nameLen] = 0;
		memcpy(&off, rec + nameLen, 4);
		memcpy(&size, rec + nameLen + 4, 4);
		a->entries[i].offset = base + off;
		a->entries[i].size = size;
	}
	free(raw);
	return a;
}

ArcRead_t* ArcRead_OpenFile(FILE* f, const char* path)
{
	ArcRead_t* archive = ReadIndex(f, ReadStdio, path);
	if(archive)
		archive->f = f;
	else
		fclose(f);
	return archive;
}

// close the file and free the index; NULL is allowed
void ArcRead_Close(ArcRead_t* a)
{
	if(!a)
		return;
	if(a->f)
		fclose(a->f);
	free(a->entries);
	free(a);
}

// string equality ignoring the case of ASCII letters (Shift-JIS bytes compare as they are)
static int EqualNoCase(const char* a, const char* b)
{
	for(; *a && *b; a++, b++)
	{
		int x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
		int y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
		if(x != y)
			return 0;
	}
	return *a == *b;
}

// the first entry whose name equals `name` ignoring ASCII case, or NULL
const ArcEntry_t* ArcRead_Find(const ArcRead_t* a, const char* name)
{
	uint32_t i;
	for(i = 0; i < a->count; i++)
		if(EqualNoCase(a->entries[i].name, name))
			return &a->entries[i];
	return NULL;
}

/* read the entry into a new buffer (BGI_Free it) and decode it when it is a
 * DSC stream, in which case *wasDsc is set and *size is the decoded size;
 * otherwise *size is the stored size.  The buffer has one spare byte after
 * the data.  NULL when the file cannot be read. */
uint8_t* ArcRead_Load(const ArcRead_t* a, const ArcEntry_t* e, uint32_t* size, int* wasDsc)
{
	uint8_t* raw = (uint8_t*)BGI_Alloc(e->size + 1);
	uint8_t* out;
	*wasDsc = 0;
	if(fseek(a->f, (long)e->offset, SEEK_SET) != 0 || fread(raw, 1, e->size, a->f) != e->size)
	{
		BGI_Free(raw);
		return NULL;
	}
	if(e->size >= sizeof(DscHeader_t) && BGI_IsDsc(raw))
	{
		uint32_t n = BGI_DscOutSize(raw);
		out = (uint8_t*)BGI_Alloc(n + 1);
		*size = BGI_DscDecode(raw, out);
		BGI_Free(raw);
		*wasDsc = 1;
		return out;
	}
	*size = e->size;
	return raw;
}

/* Shared with the VM's 81 39 without exposing the tools' index type. */
int ArcRead_ListNames(const char* path, char* buffer, uint32_t* sizeOrCount)
{
	OsFile_t* file = path ? OS_FileOpenRead(path) : NULL;
	ArcRead_t* archive;
	uint32_t bytes = 0;
	if(sizeOrCount)
		*sizeOrCount = 0;
	if(!file)
		return 0;
	archive = ReadIndex(file, ReadGameFile, path);
	OS_FileClose(file);
	if(!archive)
		return 0;
	for(uint32_t i = 0; i < archive->count; ++i)
	{
		const char* name = archive->entries[i].name;
		size_t size = strlen(name) + 1;
		if(buffer)
			memcpy(buffer + bytes, name, size);
		bytes += (uint32_t)size;
	}
	if(sizeOrCount)
		*sizeOrCount = buffer ? archive->count : bytes;
	ArcRead_Close(archive);
	return 1;
}
