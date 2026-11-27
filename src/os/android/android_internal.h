/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef BGI_ANDROID_INTERNAL_H
#define BGI_ANDROID_INTERNAL_H

#include "bgi/os.h"
#include "audio.h"
#include "clock.h"

#include <android/native_window.h>
#include <android/input.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>

/* Java only copies input here; all VM callbacks stay on the engine thread. */
typedef struct AndroidInputEvent
{
	int type, action, key, repeat, width, height;
	float x, y, wheel;
} AndroidInputEvent_t;

#define ANDROID_INPUT_CAPACITY 256
enum { ANDROID_POINTER_MOVE, ANDROID_POINTER_DOWN, ANDROID_POINTER_UP, ANDROID_POINTER_WHEEL, ANDROID_POINTER_CANCEL };

typedef struct AndroidHost
{
	int (*run)(struct AndroidHost*);
	void* backend;
	pthread_mutex_t lock;
	pthread_cond_t changed;
	JavaVM* vm;
	jobject activity;
	jmethodID engineFinished, messageBox, editCommand, inputDialog;
	char formText[4][256];
	int formCount, formLimits[4], formMonth, formDay;
	unsigned dialogSerial;
	int dialogPending, dialogResult;
	char editText[1024];
	int editRevision, editLimit, editIme;
	ANativeWindow* window;
	AndroidInputEvent_t input[ANDROID_INPUT_CAPACITY];
	unsigned inputCount;
	int inputReset;
	char internal[PATH_MAX];
	char external[PATH_MAX];
	unsigned redraw;
	uint64_t frameSerial;
	int focused, resumed, destroyed, exited, scaleMode;
} AndroidHost_t;

extern AndroidHost_t* gAndroidHost; // only the engine thread reads this pointer
int OsAndroid_InitPaths(void);
void OsAndroid_EngineOptions(char* out, size_t size);
void OsAndroid_Log(const char* text);
void OsAndroid_ReportVm(int final);
void OsAndroid_ReportError(const char* body);
size_t OsAndroid_ScriptToUtf8(const char* text,char* out,size_t size);
void OsAndroid_MovieForeground(int resumed);
int OsAndroid_MovieStatus(void);
int OsAndroid_MovieTick(void);
int OsAndroid_MovieSkip(void);
JNIEnv* OsAndroid_GetEnv(int* attached);
void OsAndroid_ReleaseEnv(int attached);
void OsAndroid_EditCommand(int op, int a, int b, int c, int d, const char* text);
/* Shared with TextInput.java. */
enum { ANDROID_EDIT_SHOW, ANDROID_EDIT_MOVE, ANDROID_EDIT_FONT,
    ANDROID_EDIT_SELECT, ANDROID_EDIT_FOCUS, ANDROID_EDIT_COLOUR,
    ANDROID_EDIT_REFRESH, ANDROID_EDIT_REJECT, ANDROID_EDIT_LIMIT,
    ANDROID_EDIT_TEXT, ANDROID_EDIT_DESTROY, ANDROID_EDIT_CANVAS, ANDROID_EDIT_ALIGNMENT };

#endif
