/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_HOST_H
#define BGI_ANDROID_HOST_H

#include "openbgi.h"
#include "../../os/android/storage_settings.h"

OpenBGI_Host_t AndroidHostConfig(void);
void AndroidHostTextEncoding(void* user, int utf8);
/* Prepares an already mounted game. Returns an error message or NULL on success. */
const char* AndroidHostGameOptions(const AndroidGameSettings_t* settings, char* args, size_t size);

#endif
