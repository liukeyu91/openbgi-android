/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef BGI_ANDROID_INTERNAL_H
#define BGI_ANDROID_INTERNAL_H

#include "bgi/os.h"

#include <android/native_activity.h>
#include <limits.h>
#include <pthread.h>

typedef struct AndroidHost
{
	pthread_mutex_t lock;
	pthread_cond_t changed;
	ANativeActivity* activity;
	ANativeWindow* window;
	AInputQueue* input;
	char internal[PATH_MAX];
	char external[PATH_MAX];
	unsigned redraw;
	int focused, resumed, destroyed, exited;
} AndroidHost_t;

extern AndroidHost_t* gAndroidHost; // only the engine thread reads this pointer
int OsAndroid_InitPaths(void);
void OsAndroid_Log(const char* text);
void OsAndroid_AudioForeground(int resumed);

#endif
