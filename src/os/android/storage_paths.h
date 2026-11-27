/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_STORAGE_PATHS_H
#define BGI_ANDROID_STORAGE_PATHS_H

#include <stddef.h>

/* 1 for a valid id, 0 for a missing marker, -1 on a malformed or unreadable marker. */
int OsAndroid_ReadActiveGame(const char* internal, char* id, size_t size);
int OsAndroid_InitStorage(const char* internal, const char* external, void (*log)(const char*));

#endif
