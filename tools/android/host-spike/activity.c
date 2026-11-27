/* SPDX-License-Identifier: GPL-2.0-only */
#include "audio.h"
#include "fonts.h"
#include "frame.h"
#include "host.h"
#include "requests.h"
#include "android_native_app_glue.h"
#include "../../../src/os/android/clock.h"
#include "../../../src/os/android/storage_paths.h"

#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEMO "host-spike.zip"
#define TAG "OpenBGIHostSpike"

typedef struct {
    struct android_app* app;
    OpenBGI_t* engine;
    HostAudio_t audio;
    HostRequests_t requests;
    JNIEnv* env;
    jmethodID showMessage, messageResult;
    int utf8, scale;
    HostCanvas_t canvas;
    Viewport_t viewport;
    unsigned windowGeneration;
    int64_t renderRetryAt;
    uint8_t held[256], keyRefs[256];
    int pointer, x, y;
    int active, focused, redraw, shown, expose;
} Host;

void OsAndroid_Log(const char* text)
{
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", text);
}

static int EnsureDirectory(const char* path)
{
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static int WriteText(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    if(!file) return 0;
    int ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

static int DemoMatches(AAsset* asset, const char* path)
{
    FILE* file = fopen(path, "rb");
    if(!file) return 0;
    char expected[4096], actual[4096];
    int count;
    while((count = AAsset_read(asset, expected, sizeof expected)) > 0) {
        if(fread(actual, 1, (size_t)count, file) != (size_t)count ||
           memcmp(actual, expected, (size_t)count) != 0) break;
    }
    int matches = count == 0 && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return matches;
}

/* Refresh the bundled fixture across APK upgrades. */
static int InstallDemo(struct android_app* app, const char* path)
{
    char temporary[PATH_MAX];
    if(snprintf(temporary, sizeof temporary, "%s.tmp", path) >= (int)sizeof temporary) return 0;
    AAsset* asset = AAssetManager_open(app->activity->assetManager, DEMO, AASSET_MODE_STREAMING);
    if(!asset) return 0;
    if(DemoMatches(asset, path)) {
        AAsset_close(asset);
        return 1;
    }
    if(AAsset_seek64(asset, 0, SEEK_SET) < 0) {
        AAsset_close(asset);
        return 0;
    }
    FILE* file = fopen(temporary, "wb");
    char buffer[4096];
    int count = -1;
    if(file) {
        while((count = AAsset_read(asset, buffer, sizeof buffer)) > 0) {
            if(fwrite(buffer, 1, (size_t)count, file) != (size_t)count) {
                count = -1;
                break;
            }
        }
        if(fclose(file) != 0) count = -1;
    }
    AAsset_close(asset);
    if(count == 0 && rename(temporary, path) == 0) return 1;
    unlink(temporary);
    return 0;
}

static int SelectGame(struct android_app* app)
{
    const char* internal = app->activity->internalDataPath;
    const char* external = app->activity->externalDataPath;
    char path[PATH_MAX], source[PATH_MAX], id[80];
    int selected = OsAndroid_ReadActiveGame(internal, id, sizeof id);
    if(selected < 0) return 0;
    if(selected && strcmp(id, "demo"))
        return OsAndroid_InitStorage(internal, external, OsAndroid_Log);
    if(snprintf(source, sizeof source, "%s/" DEMO, internal) >= (int)sizeof source) return 0;
    if(selected) {
        if(access(source, R_OK) == 0 && !InstallDemo(app, source)) return 0;
        return OsAndroid_InitStorage(internal, external, OsAndroid_Log);
    }
    if(!InstallDemo(app, source)) return 0;
    const char* directories[] = {"games", "games/demo", "games/demo/.openbgi", "games/demo/.openbgi/data",
        "games/demo/.openbgi/data/.openbgi"};
    for(size_t i = 0; i < sizeof directories / sizeof directories[0]; ++i) {
        if(snprintf(path, sizeof path, "%s/%s", internal, directories[i]) >= (int)sizeof path ||
           !EnsureDirectory(path)) return 0;
    }
    if(snprintf(path, sizeof path, "%s/games/demo/.openbgi/data/.openbgi/original-saves.txt", internal) >= (int)sizeof path ||
       (access(path, F_OK) != 0 && !WriteText(path, ""))) return 0;
    if(snprintf(path, sizeof path, "%s/games/demo/.openbgi/source.txt", internal) >= (int)sizeof path ||
       !WriteText(path, source)) return 0;
    if(snprintf(path, sizeof path, "%s/active-game.txt", internal) >= (int)sizeof path ||
       !WriteText(path, "demo")) return 0;
    return OsAndroid_InitStorage(internal, external, OsAndroid_Log);
}


static void ReleasePointer(Host* h)
{
    if(h->pointer >= 0) OpenBGI_Button(h->engine, OPENBGI_BUTTON_LEFT, 0, h->x, h->y, 0);
    h->pointer = -1;
}

static void Present(Host* h, const OpenBGI_Frame_t* frame)
{
    if(frame->expose == OPENBGI_EXPOSE_NONE) {
        h->canvas.bpp = 0;
        return;
    }
    static const uint32_t black = UINT32_C(0xff000000);
    AndroidDamage_t damage = {0};
    const uint32_t* pixels = &black;
    int width = 1, height = 1, expose = OPENBGI_EXPOSE_BLACK;
    if(frame->expose == OPENBGI_EXPOSE_PICTURE && HostCanvasUpdate(&h->canvas, frame, &damage)) {
        pixels = h->canvas.pixels;
        width = h->canvas.width;
        height = h->canvas.height;
        expose = OPENBGI_EXPOSE_PICTURE;
    } else h->canvas.bpp = 0;

    if(!h->redraw && expose == h->expose && damage.l == damage.r) return;
    int64_t now = (int64_t)OsAndroid_MonotonicNs();
    if(now && now < h->renderRetryAt) return;
    Viewport_t visible;
    int drawn = OsAndroid_Render(h->app->window, h->windowGeneration, pixels, width, height,
        damage, h->scale, &visible);
    h->redraw = drawn <= 0;
    h->renderRetryAt = drawn < 0 ? now + INT64_C(1000000000) : 0;
    if(drawn <= 0 || expose == OPENBGI_EXPOSE_BLACK) {
        ReleasePointer(h);
        h->viewport = (Viewport_t){0};
    } else h->viewport = visible;
    if(drawn <= 0) return;
    h->expose = expose;
    if(expose == OPENBGI_EXPOSE_PICTURE && !h->shown) {
        h->shown = 1;
        __android_log_print(ANDROID_LOG_INFO, TAG, "Frame %dx%d/%d", width, height, frame->bpp);
    }
}

static void ReleaseInput(Host* h)
{
    ReleasePointer(h);
    for(int vk = 1; vk < 256; ++vk)
        if(h->keyRefs[vk]) OpenBGI_Key(h->engine, vk, 0, 0);
    memset(h->keyRefs, 0, sizeof h->keyRefs);
    memset(h->held, 0, sizeof h->held);
}

static int KeyCode(int code)
{
    if(code >= AKEYCODE_A && code <= AKEYCODE_Z) return 'A' + code - AKEYCODE_A;
    if(code >= AKEYCODE_0 && code <= AKEYCODE_9) return '0' + code - AKEYCODE_0;
    if(code >= AKEYCODE_F1 && code <= AKEYCODE_F12) return 0x70 + code - AKEYCODE_F1;
    switch(code) {
    case AKEYCODE_DPAD_LEFT: return 0x25;
    case AKEYCODE_DPAD_UP: return 0x26;
    case AKEYCODE_DPAD_RIGHT: return 0x27;
    case AKEYCODE_DPAD_DOWN: return 0x28;
    case AKEYCODE_DPAD_CENTER:
    case AKEYCODE_BUTTON_A:
    case AKEYCODE_ENTER: return 0x0d;
    case AKEYCODE_BUTTON_B:
    case AKEYCODE_ESCAPE: return 0x1b;
    case AKEYCODE_SPACE: return 0x20;
    case AKEYCODE_TAB: return 0x09;
    case AKEYCODE_PAGE_UP: return 0x21;
    case AKEYCODE_PAGE_DOWN: return 0x22;
    case AKEYCODE_CTRL_LEFT:
    case AKEYCODE_CTRL_RIGHT: return 0x11;
    default: return 0;
    }
}

static int KeyInput(Host* h, AInputEvent* event)
{
    int code = AKeyEvent_getKeyCode(event);
    int vk = KeyCode(code), action = AKeyEvent_getAction(event);
    if(!vk || code < 0 || code >= (int)sizeof h->held) return 0;
    if(action == AKEY_EVENT_ACTION_DOWN) {
        if(!h->held[code]) {
            h->held[code] = (uint8_t)vk;
            if(!h->keyRefs[vk]++) OpenBGI_Key(h->engine, vk, 1, 0);
        } else if(AKeyEvent_getRepeatCount(event) > 0) OpenBGI_Key(h->engine, vk, 1, OPENBGI_KEY_REPEAT);
    } else if(action == AKEY_EVENT_ACTION_UP && h->held[code]) {
        h->held[code] = 0;
        if(!--h->keyRefs[vk]) OpenBGI_Key(h->engine, vk, 0, 0);
    }
    return 1;
}

static int32_t Input(struct android_app* app, AInputEvent* event)
{
    Host* h = app->userData;
    if(!h->active || !h->focused || h->requests.positive) return 0;
    int eventType = AInputEvent_getType(event);
    if(eventType == AINPUT_EVENT_TYPE_KEY) return KeyInput(h, event);
    Viewport_t rect = h->viewport;
    if(eventType != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(event);
    int type = action & AMOTION_EVENT_ACTION_MASK;
    if(type == AMOTION_EVENT_ACTION_CANCEL) {
        ReleasePointer(h);
        return 1;
    }
    if(rect.w <= 0 || rect.h <= 0) return 0;
    int index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    if(type == AMOTION_EVENT_ACTION_MOVE) {
        index = 0;
        while((size_t)index < AMotionEvent_getPointerCount(event) &&
              AMotionEvent_getPointerId(event, index) != h->pointer) ++index;
    }
    if((size_t)index >= AMotionEvent_getPointerCount(event)) return 0;
    int id = AMotionEvent_getPointerId(event, index), x, y;
    int w = h->canvas.width, height = h->canvas.height;
    int inside = AndroidViewportPoint(rect, w, height, AMotionEvent_getX(event, index),
        AMotionEvent_getY(event, index), &x, &y);
    if(type == AMOTION_EVENT_ACTION_SCROLL || type == AMOTION_EVENT_ACTION_HOVER_MOVE) {
        if(!inside) return 0;
        OpenBGI_Pointer(h->engine, x, y);
        if(type == AMOTION_EVENT_ACTION_SCROLL)
            OpenBGI_Wheel(h->engine, (int)(120 * AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, index)));
        return 1;
    }
    if(type == AMOTION_EVENT_ACTION_DOWN) {
        if(!inside) return 0;
        h->pointer = id;
    }
    if(id != h->pointer) return 0;
    h->x = x < 0 ? 0 : x >= w ? w - 1 : x;
    h->y = y < 0 ? 0 : y >= height ? height - 1 : y;
    OpenBGI_Pointer(h->engine, h->x, h->y);
    if(type == AMOTION_EVENT_ACTION_DOWN)
        OpenBGI_Button(h->engine, OPENBGI_BUTTON_LEFT, 1, h->x, h->y, 0);
    else if(type == AMOTION_EVENT_ACTION_UP || type == AMOTION_EVENT_ACTION_POINTER_UP)
        ReleasePointer(h);
    return 1;
}

static void Command(struct android_app* app, int32_t command)
{
    Host* h = app->userData;
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        ++h->windowGeneration;
        OsAndroid_ClockSetResumed(h->active && app->window);
        /* fall through */
    case APP_CMD_WINDOW_RESIZED:
        ReleasePointer(h);
        h->viewport = (Viewport_t){0};
        h->renderRetryAt = 0;
        /* fall through */
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        h->redraw = 1;
        break;
    case APP_CMD_RESUME:
        h->active = 1;
        OsAndroid_ClockSetResumed(app->window != NULL);
        break;
    case APP_CMD_PAUSE:
        h->active = 0;
        OsAndroid_ClockSetResumed(0);
        ReleaseInput(h);
        break;
    case APP_CMD_GAINED_FOCUS: h->focused = 1; OpenBGI_Focus(h->engine, 1); break;
    case APP_CMD_LOST_FOCUS:
        h->focused = 0;
        ReleaseInput(h);
        OpenBGI_Focus(h->engine, 0);
        break;
    case APP_CMD_TERM_WINDOW:
        OsAndroid_ClockSetResumed(0);
        ReleaseInput(h);
        OsAndroid_RenderClose();
        h->viewport = (Viewport_t){0};
        break;
    }
}

static int ClearJavaError(JNIEnv* env)
{
    if(!(*env)->ExceptionCheck(env)) return 0;
    (*env)->ExceptionClear(env);
    OsAndroid_Log("Android message dialog call failed");
    return 1;
}

static void InitMessages(Host* h)
{
    ANativeActivity* activity = h->app->activity;
    if((*activity->vm)->AttachCurrentThread(activity->vm, &h->env, NULL) != JNI_OK) {
        h->env = NULL;
        OsAndroid_Log("Could not attach the message dialog thread to Java");
        return;
    }
    JNIEnv* env = h->env;
    jclass type = (*env)->GetObjectClass(env, activity->clazz);
    if(!ClearJavaError(env) && type) {
        h->showMessage = (*env)->GetMethodID(env, type, "showEngineMessage", "([B[BIIZZ)Z");
        if(!ClearJavaError(env) && h->showMessage)
            h->messageResult = (*env)->GetMethodID(env, type, "engineMessageResult", "()I");
        ClearJavaError(env);
    }
    if(type) (*env)->DeleteLocalRef(env, type);
}

static jbyteArray MessageBytes(JNIEnv* env, const char* text)
{
    size_t n = text ? strlen(text) : 0;
    if(n > INT_MAX) return NULL;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)n);
    if(bytes && n) (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)n, (const jbyte*)text);
    return bytes;
}

static int ShowMessage(void* ui, const OpenBGI_Request_t* q, int positive, int negative)
{
    Host* h = ui;
    JNIEnv* env = h->env;
    if(!env || !h->showMessage || !h->messageResult) return 0;
    ReleaseInput(h);
    jbyteArray text = MessageBytes(env, q->u.message.text);
    jbyteArray caption = NULL;
    jboolean shown = JNI_FALSE;
    if(!ClearJavaError(env) && text) {
        caption = MessageBytes(env, q->u.message.caption);
        if(!ClearJavaError(env) && caption) {
            shown = (*env)->CallBooleanMethod(env, h->app->activity->clazz, h->showMessage,
                text, caption, (jint)positive, (jint)negative,
                (jboolean)((q->u.message.flags & OPENBGI_MB_DEFBUTTON2) != 0), (jboolean)h->utf8);
            if(ClearJavaError(env)) shown = JNI_FALSE;
        }
    }
    if(text) (*env)->DeleteLocalRef(env, text);
    if(caption) (*env)->DeleteLocalRef(env, caption);
    return shown;
}

static int MessageResult(void* ui)
{
    Host* h = ui;
    JNIEnv* env = h->env;
    jint result = (*env)->CallIntMethod(env, h->app->activity->clazz, h->messageResult);
    return ClearJavaError(env) ? -1 : result;
}

static void TextEncoding(void* user, int utf8)
{
    Host* h = user;
    h->utf8 = utf8;
    AndroidHostTextEncoding(user, utf8);
}

void android_main(struct android_app* app)
{
    if(!SelectGame(app)) {
        OsAndroid_Log("Could not open the selected library entry.");
        ANativeActivity_finish(app->activity);
        return;
    }
    AndroidGameSettings_t settings;
    OsAndroid_ReadGameSettings(&settings);
    char args[96];
    const char* error = AndroidHostGameOptions(&settings, args, sizeof args);
    if(error) {
        OsAndroid_Log(error);
        ANativeActivity_finish(app->activity);
        return;
    }
    if(!strcmp(settings.encoding, "utf8"))
        OsAndroid_Log("UTF-8 text mode must be selected by the game's boot script.");
    OsAndroid_Log(args);
    Host h = {.app = app, .pointer = -1, .expose = OPENBGI_EXPOSE_NONE, .scale = settings.scale};
    OpenBGI_Host_t api = AndroidHostConfig();
    api.user = &h;
    api.textEncoding = TextEncoding;
    api.caps = OPENBGI_CAP_MESSAGES;
    OsAndroid_ClockSetResumed(0);
    h.engine = OpenBGI_New(&api);
    if (!h.engine) {
        ANativeActivity_finish(app->activity);
        return;
    }
    app->userData = &h;
    app->onAppCmd = Command;
    app->onInputEvent = Input;
    InitMessages(&h);
    const HostMessageUi_t messages = {.show = ShowMessage, .result = MessageResult, .ui = &h};
    int run = OpenBGI_Start(h.engine, args);
    OpenBGI_Frame_t frame = {.size = sizeof frame};
    int delay = 0, stopped = 0, finished = 0, status = 0;
    while (!app->destroyRequested) {
        struct android_poll_source* source = NULL;
        int runnable = !finished && app->window && h.active;
        if (ALooper_pollOnce(runnable ? delay : -1, NULL, NULL, (void**)&source) >= 0 && source)
            source->process(app, source);
        if (app->destroyRequested) break;
        if(!h.active || !app->window || finished) {
            HostAudioUpdate(&h.audio, h.engine, 0);
            continue;
        }
        if(run == OPENBGI_RUNNING) {
            int waiting = HostRequestsPump(&h.requests, h.engine, &messages);
            HostAudioUpdate(&h.audio, h.engine, h.focused || waiting);
            if(waiting) OpenBGI_RunAudio(h.engine);
            OpenBGI_PumpEvents(h.engine);
            run = OpenBGI_RunFrame(h.engine, &frame);
            Present(&h, &frame);
            delay = waiting ? 16 : frame.idleMs < 1 ? 1 : frame.idleMs > 16 ? 16 : frame.idleMs;
        }
        if(run != OPENBGI_RUNNING && !stopped) {
            HostAudioUpdate(&h.audio, h.engine, 0);
            status = OpenBGI_Stop(h.engine);
            stopped = 1;
        }
        if(stopped) {
            finished = !HostRequestsPump(&h.requests, h.engine, &messages);
            if(finished) ANativeActivity_finish(app->activity);
            delay = 16;
        }
    }
    OsAndroid_ClockSetResumed(0);
    ReleaseInput(&h);
    HostAudioUpdate(&h.audio, h.engine, 0);
    OsAndroid_RenderClose();
    free(h.canvas.pixels);
    if(!stopped) status = OpenBGI_Stop(h.engine);
    OpenBGI_Delete(h.engine);
    AndroidHostFontsClose();
    if(h.env) (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Engine stopped: %d", status);
}
