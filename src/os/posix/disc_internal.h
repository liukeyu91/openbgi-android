/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef BGI_DISC_INTERNAL_H_
#define BGI_DISC_INTERNAL_H_

#include "posix_internal.h"

#define VOL_MAX_ENTRIES 0x400000u
#define VOL_NAME_MAX 0x400

typedef FILE VolIn_t;
typedef struct VolEntry
{
	char* path;
	uint64_t data;  // the first byte of the entry's stored data
	uint64_t size;  // the bytes the engine reads (the uncompressed size)
	uint64_t csize; // the bytes kept in the container (the compressed size of a zip entry)
	uint32_t check; // the archive's CRC-32, 0 when the format has none
	uint64_t mtime;
	uint8_t isDir, readable, stored, method; // method: 0 stored, 8 deflated (zip)
	uint8_t encrypted;
} VolEntry_t;
typedef struct VolList VolList_t;

int Iso_Read(const VolIn_t* file, uint64_t size, VolList_t* list);
int Udf_Read(const VolIn_t* file, uint64_t size, VolList_t* list);
int Zip_Read(const VolIn_t* file, uint64_t size, VolList_t* list);
uint8_t* Zip_EntryBytes(const VolIn_t* file, const VolEntry_t* entry, size_t* size);
int VolIn_ReadAt(const VolIn_t* file, uint64_t offset, void* buf, size_t size);
int VolList_Add(VolList_t* list, const char* path, const VolEntry_t* entry);
uint32_t VolList_Count(const VolList_t* list);
VolEntry_t* VolList_At(VolList_t* list, uint32_t index);
size_t Vol_PutUtf8(char* out, size_t off, size_t cap, uint32_t cp);
int Vol_PutUtf16Be(const uint8_t* raw, size_t n, char* out, size_t cap);
void Vol_SetTime(VolEntry_t* e, int year, int month, int day, int hour, int min, int sec, int zoneMin);

int Disc_Dup(void);
int Disc_Mounted(void);
const VolEntry_t* Disc_Find(const char* relative);
const VolEntry_t* Disc_Next(const char* directory, uint32_t* index, const char** name);
uint32_t Disc_Count(void);
const VolEntry_t* Disc_At(uint32_t index);
const char* Disc_Root(void);
const char* Disc_ImagePath(void);
/* A read handle on one entry: a duplicated descriptor for stored bytes (it
 * survives an unmount), or the decompressed copy of a deflated entry. */
typedef struct DiscHandle
{
	int fd;
	uint64_t offset, position;
	uint32_t size;
	uint8_t* data; // the decompressed bytes, NULL for stored entries
} DiscHandle_t;
int Disc_Open(const VolEntry_t* entry, DiscHandle_t* handle);
uint32_t Disc_Read(DiscHandle_t* handle, void* buf, uint32_t size);
void Disc_Close(DiscHandle_t* handle);
const VolEntry_t* OsPosix_DiscPath(const char* native, int localized);
const char* OsPosix_DiscDirectory(const char* native);

#endif
