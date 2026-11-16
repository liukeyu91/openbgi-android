/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * SurfaceView/JNI host. UI callbacks copy state under lock; only the
 * worker touches VM state. One game session owns the :engine process.
 */
#include "android_internal.h"
#include "bgi/engine.h"
#include "renderer.h"
#include <android/log.h>
#include <android/native_window_jni.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

AndroidHost_t* gAndroidHost;
/* Kept until process exit, so late lifecycle callbacks cannot use freed state. */
static AndroidHost_t* gHost;
/* Independent lock: clock reads also happen from asset/audio threads. */
static pthread_mutex_t gClockLock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t gPausedNs, gPauseAt;
static int gClockPaused;

static uint64_t RawClockNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void ClockState(int resumed)
{
    uint64_t now;
    pthread_mutex_lock(&gClockLock);
    now = RawClockNs();
    if(!resumed && !gClockPaused) { gPauseAt = now; gClockPaused = 1; }
    else if(resumed && gClockPaused) { gPausedNs += now - gPauseAt; gClockPaused = 0; }
    pthread_mutex_unlock(&gClockLock);
}

uint64_t OsAndroid_ClockNs(void)
{
    uint64_t result;
    pthread_mutex_lock(&gClockLock);
    result = (gClockPaused ? gPauseAt : RawClockNs()) - gPausedNs;
    pthread_mutex_unlock(&gClockLock);
    return result;
}

static int gLogFd = -1;
static pthread_mutex_t gLogLock = PTHREAD_MUTEX_INITIALIZER;
static size_t gLogBytes;

static int gCrashFd = -1;
static void RecordSignal(int number)
{
    /* ART sigchain does not reliably implement SA_RESETHAND. Re-raising can
     * reenter this handler forever instead of terminating. Keep the minimal
     * record, then stop the engine with only async-signal-safe operations.
     * Intercepted native signals therefore do not promise a system tombstone. */
    char message[] = "Native fatal signal 00\n";
    message[20] = '0' + number / 10;
    message[21] = '0' + number % 10;
    if(gCrashFd >= 0) write(gCrashFd, message, sizeof message - 1);
    _exit(128 + number);
}

void OsAndroid_Log(const char* text)
{
	__android_log_write(ANDROID_LOG_INFO, "OpenBGI", text);
    pthread_mutex_lock(&gLogLock);
    if(gLogFd >= 0) {
        size_t length = strlen(text);
        if(gLogBytes + length > 2 * 1024 * 1024) {
            ftruncate(gLogFd, 0); lseek(gLogFd, 0, SEEK_SET); gLogBytes = 0;
        }
        write(gLogFd, text, length); write(gLogFd, "\n", 1);
        gLogBytes += length + 1;
    }
    pthread_mutex_unlock(&gLogLock);
}


static void* RunEngine(void* context)
{
    AndroidHost_t* host = context;
    JNIEnv* env = NULL;
    int destroyed;
    pthread_setname_np(pthread_self(), "OpenBGI-VM");
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
            {
                char options[128];
                OsAndroid_EngineOptions(options, sizeof options);
                Engine_Main(options);
            }
            else
                OsAndroid_Log("Game missing: import a directory containing system.arc or ipl._bp.");
            OS_Shutdown();
            OsAndroid_RenderClose();
        }
        else
            OsAndroid_Log("Could not open the app's game or configuration directory.");
    }
    gAndroidHost = NULL;
    pthread_mutex_lock(&host->lock);
    host->exited = 1;
    pthread_mutex_unlock(&host->lock);
    if((*host->vm)->AttachCurrentThread(host->vm, &env, NULL) == JNI_OK)
    {
        (*env)->CallVoidMethod(env, host->activity, host->engineFinished);
        if(!(*env)->ExceptionCheck(env))
        {
            (*host->vm)->DetachCurrentThread(host->vm);
            return NULL; /* Java finishes the Activity then terminates this process. */
        }
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        (*host->vm)->DetachCurrentThread(host->vm);
    }
    _exit(0);
    return NULL;
}

JNIEXPORT jboolean JNICALL Java_org_openbgi_port_GameActivity_nativeCreate(
    JNIEnv* env, jobject activity, jstring internalPath, jstring externalPath, jint scaleMode)
{
    AndroidHost_t* host;
    pthread_t worker;
    const char* internal;
    const char* external;
    jclass cls;
    if(gHost) return JNI_FALSE;
    internal = (*env)->GetStringUTFChars(env, internalPath, NULL);
    if(!internal) return JNI_FALSE;
    external = (*env)->GetStringUTFChars(env, externalPath, NULL);
    if(!external) { (*env)->ReleaseStringUTFChars(env, internalPath, internal); return JNI_FALSE; }
    pthread_mutex_lock(&gLogLock);
    if(gLogFd < 0 && internal) {
        char dir[4096], current[4096], previous[4096];
        snprintf(dir, sizeof dir, "%s/logs", internal);
        mkdir(dir, 0700);
        snprintf(current, sizeof current, "%s/logs/engine-current.txt", internal);
        snprintf(previous, sizeof previous, "%s/logs/engine-previous.txt", internal);
        rename(current, previous);
        snprintf(dir, sizeof dir, "%s/logs/native-crash.txt", internal);
        gCrashFd = open(dir, O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0600);
        if(gCrashFd >= 0) {
            struct sigaction action;
            struct stat status;
            int signals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
            size_t i;
            /* Bound legacy logs before installing handlers, never in one. */
            if(fstat(gCrashFd, &status) == 0 && status.st_size > 64 * 1024) {
                static const char reset[] = "Older native fatal signal records truncated\n";
                if(ftruncate(gCrashFd, 0) == 0) write(gCrashFd, reset, sizeof reset - 1);
            }
            memset(&action, 0, sizeof action);
            action.sa_handler = RecordSignal;
            sigemptyset(&action.sa_mask);
            for(i = 0; i < sizeof signals / sizeof signals[0]; ++i)
                sigaction(signals[i], &action, NULL);
        }
        gLogFd = open(current, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
        if(gLogFd >= 0) {
            dup2(gLogFd, STDERR_FILENO);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
    }
    pthread_mutex_unlock(&gLogLock);

    ClockState(0);
    host = calloc(1, sizeof *host);
    if(!host) goto failed;
    if(pthread_mutex_init(&host->lock, NULL) != 0) { free(host); goto failed; }
    if(pthread_cond_init(&host->changed, NULL) != 0)
    { pthread_mutex_destroy(&host->lock); free(host); goto failed; }
    (*env)->GetJavaVM(env, &host->vm);
    host->scaleMode = scaleMode>=0 && scaleMode<=2 ? scaleMode : 0;
    host->activity = (*env)->NewGlobalRef(env, activity);
    cls = (*env)->GetObjectClass(env, activity);
    host->engineFinished = (*env)->GetMethodID(env, cls, "engineFinished", "()V");
    host->messageBox = (*env)->GetMethodID(env, cls, "showMessageBox", "([B[BII)V");
    host->editCommand = (*env)->GetMethodID(env, cls, "editCommand", "(IIIIIII[B)V");
    host->inputDialog = (*env)->GetMethodID(env, cls, "showInputDialog", "(II[B[[B[[B[I[IIIZ)V");
    (*env)->DeleteLocalRef(env, cls);
    snprintf(host->internal, sizeof host->internal, "%s", internal);
    snprintf(host->external, sizeof host->external, "%s", external);
    if(!host->activity || !host->engineFinished || !host->messageBox || !host->editCommand || !host->inputDialog || pthread_create(&worker, NULL, RunEngine, host) != 0)
    {
        if(host->activity) (*env)->DeleteGlobalRef(env, host->activity);
        pthread_cond_destroy(&host->changed);
        pthread_mutex_destroy(&host->lock);
        free(host);
        goto failed;
    }
    gHost = host;
    pthread_detach(worker);
    (*env)->ReleaseStringUTFChars(env, internalPath, internal);
    (*env)->ReleaseStringUTFChars(env, externalPath, external);
    OsAndroid_Log("SurfaceView/JNI engine host started.");
    return JNI_TRUE;
failed:
    (*env)->ReleaseStringUTFChars(env, internalPath, internal);
    (*env)->ReleaseStringUTFChars(env, externalPath, external);
    OsAndroid_Log("Could not start the engine thread.");
    return JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeSurface(JNIEnv* env, jobject activity, jobject surface)
{
    ANativeWindow* window;
    AndroidHost_t* host = gHost;
    BGI_UNUSED(activity);
    if(!host) return;
    window = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    pthread_mutex_lock(&host->lock);
    if(host->window) ANativeWindow_release(host->window);
    host->window = window;
    host->redraw++;
    host->inputCount = 0;
    host->inputReset = 1;
    pthread_cond_broadcast(&host->changed);
    pthread_mutex_unlock(&host->lock);
}

/* Choreographer only grants frame tokens. The worker owns EGL and the canvas. */
JNIEXPORT jint JNICALL Java_org_openbgi_port_GameActivity_nativeAudioStatus(JNIEnv* env,jobject activity)
{
    (void)env;(void)activity;
    return OsAndroid_AudioStatus();
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeVsync(JNIEnv* env,jobject activity)
{
    AndroidHost_t* host=gHost;
    BGI_UNUSED(env);BGI_UNUSED(activity);
    if(!host)return;
    pthread_mutex_lock(&host->lock);
    if(host->resumed && !host->destroyed && host->window)++host->frameSerial;
    pthread_mutex_unlock(&host->lock);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeState(JNIEnv* env, jobject activity, jboolean resumed, jboolean focused)
{
    AndroidHost_t* host = gHost;
    BGI_UNUSED(env); BGI_UNUSED(activity);
    if(!host) return;
    OsAndroid_AudioForeground(resumed);
    ClockState(resumed);
    pthread_mutex_lock(&host->lock);
    if(host->resumed != resumed) OsAndroid_Log(resumed ? "Host running" : "Host paused");
    host->resumed = resumed;
    host->focused = focused;
    if(!resumed || !focused) { host->inputCount = 0; host->inputReset = 1; }
    pthread_cond_broadcast(&host->changed);
    pthread_mutex_unlock(&host->lock);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeDestroy(JNIEnv* env, jobject activity)
{
    AndroidHost_t* host = gHost;
    BGI_UNUSED(env); BGI_UNUSED(activity);
    if(!host) return;
    OsAndroid_AudioForeground(0);
    pthread_mutex_lock(&host->lock);
    host->destroyed = 1;
    host->inputCount = 0;
    if(host->window) { ANativeWindow_release(host->window); host->window = NULL; }
    pthread_cond_broadcast(&host->changed);
    pthread_mutex_unlock(&host->lock);
}

/* Coalesce moves, never silently drop a key/button release on overflow. */
static void QueueInput(AndroidInputEvent_t event)
{
    AndroidHost_t* host = gHost;
    unsigned i;
    if(!host) return;
    pthread_mutex_lock(&host->lock);
    if(host->destroyed || !host->resumed || !host->focused) goto done;
    if(event.type == AINPUT_EVENT_TYPE_MOTION && event.action == ANDROID_POINTER_MOVE &&
        host->inputCount && host->input[host->inputCount - 1].type == AINPUT_EVENT_TYPE_MOTION &&
        host->input[host->inputCount - 1].action == ANDROID_POINTER_MOVE)
    { host->input[host->inputCount - 1] = event; goto done; }
    if(host->inputCount == ANDROID_INPUT_CAPACITY)
    {
        for(i = 0; i < host->inputCount; ++i)
            if(host->input[i].type == AINPUT_EVENT_TYPE_MOTION && host->input[i].action == ANDROID_POINTER_MOVE) break;
        if(i < host->inputCount)
        {
            memmove(host->input + i, host->input + i + 1, (host->inputCount - i - 1) * sizeof event);
            host->inputCount--;
        }
        else { host->inputCount = 0; host->inputReset = 1; }
    }
    host->input[host->inputCount++] = event;
done:
    pthread_mutex_unlock(&host->lock);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeKey(JNIEnv* env, jobject activity, jint action, jint key, jint repeat)
{
    AndroidInputEvent_t e = {0};
    BGI_UNUSED(env); BGI_UNUSED(activity);
    e.type = AINPUT_EVENT_TYPE_KEY; e.action = action; e.key = key; e.repeat = repeat != 0;
    QueueInput(e);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativePointer(JNIEnv* env, jobject activity,
    jint action, jint button, jfloat x, jfloat y, jfloat wheel, jint width, jint height)
{
    AndroidInputEvent_t e = {0};
    BGI_UNUSED(env); BGI_UNUSED(activity);
    e.type = AINPUT_EVENT_TYPE_MOTION; e.action = action; e.key = button;
    e.x = x; e.y = y; e.wheel = wheel; e.width = width; e.height = height;
    QueueInput(e);
}

/* Attach only for the duration of a platform service; UI methods post their
 * work, so the worker never holds the host lock across a Java call. */
JNIEnv* OsAndroid_GetEnv(int* attached)
{
    JNIEnv* env = NULL;
    AndroidHost_t* host = gAndroidHost;
    *attached = 0;
    if(!host) return NULL;
    if((*host->vm)->GetEnv(host->vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if((*host->vm)->AttachCurrentThread(host->vm, &env, NULL) != JNI_OK) return NULL;
    *attached = 1;
    return env;
}

void OsAndroid_ReleaseEnv(int attached)
{
    if(attached) (*gAndroidHost->vm)->DetachCurrentThread(gAndroidHost->vm);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeDialogResult(JNIEnv* env, jobject activity, jint serial, jint result)
{
    AndroidHost_t* host = gHost;
    BGI_UNUSED(env); BGI_UNUSED(activity);
    if(!host) return;
    pthread_mutex_lock(&host->lock);
    if(host->dialogPending && host->dialogSerial == (unsigned)serial)
    {
        host->dialogResult = result;
        host->dialogPending = 0;
        pthread_cond_broadcast(&host->changed);
    }
    pthread_mutex_unlock(&host->lock);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeEditChanged(JNIEnv* env, jobject activity,
    jbyteArray text, jint revision, jboolean ime)
{
    AndroidHost_t* host = gHost;
    jsize n;
    BGI_UNUSED(activity);
    if(!host) return;
    n = text ? (*env)->GetArrayLength(env, text) : 0;
    if(n >= (jsize)sizeof host->editText) return;
    pthread_mutex_lock(&host->lock);
    if(!host->destroyed && host->editRevision == revision && (!host->editLimit || n <= host->editLimit))
    {
        if(text) {
            (*env)->GetByteArrayRegion(env, text, 0, n, (jbyte*)host->editText);
            host->editText[n] = 0;
        }
        host->editIme = ime;
    }
    pthread_mutex_unlock(&host->lock);
}

JNIEXPORT void JNICALL Java_org_openbgi_port_GameActivity_nativeInputResult(JNIEnv* env, jobject activity,
    jint serial, jobjectArray values, jint month, jint day)
{
    AndroidHost_t* host = gHost;
    char fields[4][256] = {{0}};
    int i, count = values ? (*env)->GetArrayLength(env, values) : 0;
    BGI_UNUSED(activity);
    if(!host || count > 4) return;
    for(i = 0; i < count; ++i)
    {
        jbyteArray bytes = (*env)->GetObjectArrayElement(env, values, i);
        jsize n = bytes ? (*env)->GetArrayLength(env, bytes) : 256;
        if(n >= 256) { if(bytes) (*env)->DeleteLocalRef(env, bytes); return; }
        (*env)->GetByteArrayRegion(env, bytes, 0, n, (jbyte*)fields[i]);
        (*env)->DeleteLocalRef(env, bytes);
    }
    pthread_mutex_lock(&host->lock);
    if(host->dialogPending && host->dialogSerial == (unsigned)serial && (!values || count == host->formCount))
    {
        int valid = values != NULL;
        for(i = 0; i < count; ++i)
            if((int)strlen(fields[i]) > host->formLimits[i]) valid = 0;
        if(count == 4 && (month < 0 || month > 11 || day < 0 || day > 30)) valid = 0;
        if(valid) memcpy(host->formText, fields, sizeof fields);
        host->formMonth = month; host->formDay = day;
        host->dialogResult = valid;
        host->dialogPending = 0;
        pthread_cond_broadcast(&host->changed);
    }
    pthread_mutex_unlock(&host->lock);
}
