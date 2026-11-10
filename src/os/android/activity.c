/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * The Activity callbacks run on Android's UI thread. The VM owns its
 * usual single engine thread and observes the current window and input
 * queue under the same lock the callbacks use to detach them.
 */
#include "android_internal.h"
#include "bgi/engine.h"

#include <android/log.h>
#include <android/window.h>

AndroidHost_t* gAndroidHost;
static pthread_mutex_t gRunLock = PTHREAD_MUTEX_INITIALIZER;
static int gRunning;

void OsAndroid_Log(const char* text)
{
	__android_log_write(ANDROID_LOG_INFO, "OpenBGI", text);
}

static void FreeHost(AndroidHost_t* host)
{
	pthread_cond_destroy(&host->changed);
	pthread_mutex_destroy(&host->lock);
	free(host);
}

static void OnDestroy(ANativeActivity* activity)
{
	AndroidHost_t* host = activity->instance;
	int exited;
	pthread_mutex_lock(&host->lock);
	host->destroyed = 1;
	host->activity = NULL;
	host->input = NULL;
	if(host->window)
	{
		ANativeWindow_release(host->window);
		host->window = NULL;
	}
	exited = host->exited;
	pthread_cond_signal(&host->changed);
	pthread_mutex_unlock(&host->lock);
	if(exited)
		FreeHost(host);
}

static void OnWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
	AndroidHost_t* host = activity->instance;
	pthread_mutex_lock(&host->lock);
	if(host->window)
		ANativeWindow_release(host->window);
	ANativeWindow_acquire(window);
	host->window = window;
	ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
	host->redraw++;
	pthread_cond_signal(&host->changed);
	pthread_mutex_unlock(&host->lock);
}

static void OnWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
	AndroidHost_t* host = activity->instance;
	pthread_mutex_lock(&host->lock);
	if(host->window == window)
	{
		ANativeWindow_release(window);
		host->window = NULL;
	}
	pthread_mutex_unlock(&host->lock);
}

static void OnRedraw(ANativeActivity* activity, ANativeWindow* window)
{
	AndroidHost_t* host = activity->instance;
	BGI_UNUSED(window);
	pthread_mutex_lock(&host->lock);
	host->redraw++;
	pthread_mutex_unlock(&host->lock);
}

static void OnInputCreated(ANativeActivity* activity, AInputQueue* queue)
{
	AndroidHost_t* host = activity->instance;
	pthread_mutex_lock(&host->lock);
	host->input = queue;
	pthread_mutex_unlock(&host->lock);
}

static void OnInputDestroyed(ANativeActivity* activity, AInputQueue* queue)
{
	AndroidHost_t* host = activity->instance;
	pthread_mutex_lock(&host->lock);
	if(host->input == queue)
		host->input = NULL;
	pthread_mutex_unlock(&host->lock);
}

static void OnFocus(ANativeActivity* activity, int focused)
{
	AndroidHost_t* host = activity->instance;
	pthread_mutex_lock(&host->lock);
	host->focused = focused;
	pthread_mutex_unlock(&host->lock);
}

static void SetResumed(ANativeActivity* activity, int resumed)
{
	AndroidHost_t* host = activity->instance;
	OsAndroid_AudioForeground(resumed);
	pthread_mutex_lock(&host->lock);
	host->resumed = resumed;
	pthread_mutex_unlock(&host->lock);
}

static void OnResume(ANativeActivity* activity)
{
	SetResumed(activity, 1);
}

static void OnPause(ANativeActivity* activity)
{
	SetResumed(activity, 0);
}

static void* RunEngine(void* context)
{
	AndroidHost_t* host = context;
	int destroyed;
	pthread_mutex_lock(&host->lock);
	while(!host->window && !host->destroyed)
		pthread_cond_wait(&host->changed, &host->lock);
	destroyed = host->destroyed;
	pthread_mutex_unlock(&host->lock);

	gAndroidHost = host;
	if(!destroyed)
	{
		if(OS_Init())
		{
			if(OS_FileAttrs("system.arc") != OS_INVALID_ATTRS || OS_FileAttrs("ipl._bp") != OS_INVALID_ATTRS)
				Engine_Main("--engine=1.616 --productname=FriendToLoverHD --text-encoding=gbk");
			else
				OsAndroid_Log("Game missing: place the Fureraba HD archives in the app's files/game directory.");
			OS_Shutdown();
		}
		else
			OsAndroid_Log("Could not open the app's game or configuration directory.");
	}
	gAndroidHost = NULL;

	pthread_mutex_lock(&gRunLock);
	gRunning = 0;
	pthread_mutex_unlock(&gRunLock);
	pthread_mutex_lock(&host->lock);
	destroyed = host->destroyed;
	host->exited = 1;
	if(host->activity)
		ANativeActivity_finish(host->activity);
	pthread_mutex_unlock(&host->lock);
	if(destroyed)
		FreeHost(host);
	return NULL;
}

void ANativeActivity_onCreate(ANativeActivity* activity, void* state, size_t size)
{
	AndroidHost_t* host;
	pthread_t worker;
	BGI_UNUSED(state);
	BGI_UNUSED(size);
	pthread_mutex_lock(&gRunLock);
	if(gRunning)
	{
		pthread_mutex_unlock(&gRunLock);
		ANativeActivity_finish(activity);
		return;
	}
	gRunning = 1;
	pthread_mutex_unlock(&gRunLock);
	host = calloc(1, sizeof *host);
	if(!host)
		goto failed;
	if(pthread_mutex_init(&host->lock, NULL) != 0)
	{
		free(host);
		goto failed;
	}
	if(pthread_cond_init(&host->changed, NULL) != 0)
	{
		pthread_mutex_destroy(&host->lock);
		free(host);
		goto failed;
	}
	host->activity = activity;
	snprintf(host->internal, sizeof host->internal, "%s", activity->internalDataPath ? activity->internalDataPath : "");
	snprintf(host->external, sizeof host->external, "%s", activity->externalDataPath ? activity->externalDataPath : "");
	if(pthread_create(&worker, NULL, RunEngine, host) != 0)
	{
		FreeHost(host);
		goto failed;
	}
	pthread_detach(worker);
	activity->instance = host;
	activity->callbacks->onDestroy = OnDestroy;
	activity->callbacks->onResume = OnResume;
	activity->callbacks->onPause = OnPause;
	activity->callbacks->onWindowFocusChanged = OnFocus;
	activity->callbacks->onNativeWindowCreated = OnWindowCreated;
	activity->callbacks->onNativeWindowResized = OnRedraw;
	activity->callbacks->onNativeWindowDestroyed = OnWindowDestroyed;
	activity->callbacks->onInputQueueCreated = OnInputCreated;
	activity->callbacks->onInputQueueDestroyed = OnInputDestroyed;
	ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
	return;

failed:
	pthread_mutex_lock(&gRunLock);
	gRunning = 0;
	pthread_mutex_unlock(&gRunLock);
	OsAndroid_Log("Could not start the engine thread.");
	ANativeActivity_finish(activity);
}
