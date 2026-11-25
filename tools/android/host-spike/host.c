/* SPDX-License-Identifier: GPL-2.0-only */
#include "openbgi.h"
#include "android_native_app_glue.h"

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DEMO "host-spike.zip"
#define TAG "OpenBGIHostSpike"

typedef struct {
    struct android_app* app;
    OpenBGI_t* engine;
    AAudioStream* audio;
    OpenBGI_Rect_t viewport;
    int width, height, pointer, x, y;
    int active, redraw, shown;
} Host;

static uint32_t Ticks(void* user)
{
    (void)user;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void* AssetOpen(void* user, const char* path, int mode)
{
    Host* h = user;
    if (mode != OPENBGI_OPEN_READ || strcmp(path, DEMO) != 0)
        return NULL;
    return AAssetManager_open(h->app->activity->assetManager, path, AASSET_MODE_RANDOM);
}

static void AssetClose(void* user, void* file) { (void)user; AAsset_close(file); }

static uint32_t AssetRead(void* user, void* file, void* out, uint32_t n)
{
    (void)user;
    int count = AAsset_read(file, out, n);
    return count > 0 ? (uint32_t)count : 0;
}

static int AssetSeek(void* user, void* file, uint64_t position)
{
    (void)user;
    return position <= INT64_MAX && AAsset_seek64(file, (int64_t)position, SEEK_SET) >= 0;
}

static uint64_t AssetLength(void* user, void* file)
{
    (void)user;
    return (uint64_t)AAsset_getLength64(file);
}

static int AssetStat(void* user, const char* path, OpenBGI_NativeStat_t* result)
{
    if (strncmp(path, "mem/", 4) == 0) {
        if (result) *result = (OpenBGI_NativeStat_t){0};
        return OPENBGI_NATIVE_DIR;
    }
    void* file = AssetOpen(user, path, OPENBGI_OPEN_READ);
    if (!file) return OPENBGI_NATIVE_NONE;
    if (result) *result = (OpenBGI_NativeStat_t){.size = AssetLength(user, file)};
    AssetClose(user, file);
    return OPENBGI_NATIVE_FILE;
}

static void NativeAbsolute(void* user, const char* path, char* out, size_t n)
{
    (void)user;
    snprintf(out, n, "%s", path);
}

static void ExeDir(void* user, char* out, size_t n) { NativeAbsolute(user, "C:\\", out, n); }
static void MemoryDir(void* user, char* out, size_t n) { NativeAbsolute(user, "mem/", out, n); }

static int SetGameDir(void* user, const char* path)
{
    (void)user;
    return strncmp(path, "mem/", 4) == 0;
}

static uint32_t DriveType(void* user, const char* root)
{
    (void)user;
    return (root[0] | 32) == 'c' ? OPENBGI_DRIVE_FIXED : OPENBGI_DRIVE_NO_ROOT;
}

static const OpenBGI_Files_t kFiles = {
    .size = sizeof(OpenBGI_Files_t), .close = AssetClose, .read = AssetRead, .seek = AssetSeek,
    .length = AssetLength, .exeDir = ExeDir, .driveType = DriveType, .nativeSep = '/', .nativeOpen = AssetOpen,
    .nativeStat = AssetStat, .nativeMkdir = SetGameDir, .nativeAbsolute = NativeAbsolute,
    .gameDir = MemoryDir, .setGameDir = SetGameDir, .userDataDir = MemoryDir,
};

static aaudio_data_callback_result_t AudioData(AAudioStream* stream, void* engine,
                                                void* output, int32_t frames)
{
    (void)stream;
    OpenBGI_AudioRead(engine, output, frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void CloseAudio(Host* h)
{
    if (h->audio) {
        AAudioStream_close(h->audio);
        h->audio = NULL;
    }
    OpenBGI_SetAudioLead(h->engine, 0);
}

static void OpenAudio(Host* h)
{
    AAudioStreamBuilder* builder;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result == AAUDIO_OK) {
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setSampleRate(builder, OPENBGI_AUDIO_RATE);
        AAudioStreamBuilder_setChannelCount(builder, 2);
        AAudioStreamBuilder_setDataCallback(builder, AudioData, h->engine);
        result = AAudioStreamBuilder_openStream(builder, &h->audio);
        AAudioStreamBuilder_delete(builder);
    }
    if (result == AAUDIO_OK) {
        OpenBGI_SetAudioLead(h->engine, 50);
        OpenBGI_RunAudio(h->engine);
        result = AAudioStream_requestStart(h->audio);
    }
    if (result != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "AAudio: %s", AAudio_convertResultToText(result));
        CloseAudio(h);
    } else {
        __android_log_print(ANDROID_LOG_INFO, TAG, "AAudio: %d Hz", AAudioStream_getSampleRate(h->audio));
    }
}

static void Present(Host* h, const OpenBGI_Frame_t* frame)
{
    ANativeWindow_Buffer out;
    ANativeWindow* window = h->app->window;
    if (frame->expose == OPENBGI_EXPOSE_NONE || ANativeWindow_lock(window, &out, NULL) != 0)
        return;
    int draw = frame->expose != OPENBGI_EXPOSE_BLACK && frame->pixels && frame->width > 0 &&
        frame->height > 0 && (frame->bpp == 16 || frame->bpp == 32);
    OpenBGI_Rect_t rect = {0};
    if (draw) {
        rect.w = out.width;
        rect.h = (int)((int64_t)out.width * frame->height / frame->width);
        if (rect.h > out.height) {
            rect.h = out.height;
            rect.w = (int)((int64_t)out.height * frame->width / frame->height);
        }
        rect.x = (out.width - rect.w) / 2;
        rect.y = (out.height - rect.h) / 2;
    }
    if (out.format == WINDOW_FORMAT_RGBA_8888) {
        uint8_t* output = out.bits;
        for (int y = 0; y < out.height; ++y) {
            uint8_t* dst = output + (size_t)y * out.stride * 4;
            if (y < rect.y || y >= rect.y + rect.h || rect.w <= 0) {
                memset(dst, 0, (size_t)out.width * 4);
                continue;
            }
            const uint8_t* src = frame->pixels + (size_t)((int64_t)(y - rect.y) *
                frame->height / rect.h) * frame->pitch;
            memset(dst, 0, (size_t)rect.x * 4);
            for (int x = 0; x < rect.w; ++x) {
                const uint8_t* pixel = src + (size_t)((int64_t)x * frame->width / rect.w) * (frame->bpp / 8);
                uint8_t* rgba = dst + (size_t)(x + rect.x) * 4;
                if (frame->bpp == 32) {
                    rgba[0] = pixel[2]; rgba[1] = pixel[1]; rgba[2] = pixel[0];
                } else {
                    unsigned rgb = pixel[0] | ((unsigned)pixel[1] << 8);
                    rgba[0] = (rgb >> 10 & 31) * 255 / 31;
                    rgba[1] = (rgb >> 5 & 31) * 255 / 31;
                    rgba[2] = (rgb & 31) * 255 / 31;
                }
                rgba[3] = 255;
            }
            int right = rect.x + rect.w;
            memset(dst + right * 4, 0, (size_t)(out.width - right) * 4);
        }
    }
    ANativeWindow_unlockAndPost(window);
    h->viewport = rect;
    h->width = frame->width;
    h->height = frame->height;
    h->redraw = 0;
    if (draw && !h->shown) {
        h->shown = 1;
        __android_log_print(ANDROID_LOG_INFO, TAG, "Frame %dx%d/%d", h->width, h->height, frame->bpp);
    }
}

static void ReleasePointer(Host* h)
{
    if (h->pointer >= 0) OpenBGI_Button(h->engine, OPENBGI_BUTTON_LEFT, 0, h->x, h->y, 0);
    h->pointer = -1;
}

static int32_t Input(struct android_app* app, AInputEvent* event)
{
    Host* h = app->userData;
    OpenBGI_Rect_t rect = h->viewport;
    if (!h->active || rect.w <= 0 || rect.h <= 0 ||
        AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    int action = AMotionEvent_getAction(event);
    int type = action & AMOTION_EVENT_ACTION_MASK;
    if (type == AMOTION_EVENT_ACTION_CANCEL) {
        ReleasePointer(h);
        return 1;
    }
    int index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    if (type == AMOTION_EVENT_ACTION_MOVE) {
        index = 0;
        while ((size_t)index < AMotionEvent_getPointerCount(event) &&
            AMotionEvent_getPointerId(event, index) != h->pointer) ++index;
    }
    if ((size_t)index >= AMotionEvent_getPointerCount(event)) return 0;
    int id = AMotionEvent_getPointerId(event, index);
    int x = (int)AMotionEvent_getX(event, index) - rect.x;
    int y = (int)AMotionEvent_getY(event, index) - rect.y;
    if (type == AMOTION_EVENT_ACTION_DOWN) {
        if (x < 0 || x >= rect.w || y < 0 || y >= rect.h) return 0;
        h->pointer = id;
    }
    if (id != h->pointer) return 0;
    x = x < 0 ? 0 : x >= rect.w ? rect.w - 1 : x;
    y = y < 0 ? 0 : y >= rect.h ? rect.h - 1 : y;
    h->x = (int)((int64_t)x * h->width / rect.w);
    h->y = (int)((int64_t)y * h->height / rect.h);
    OpenBGI_Pointer(h->engine, h->x, h->y);
    if (type == AMOTION_EVENT_ACTION_DOWN) {
        OpenBGI_Button(h->engine, OPENBGI_BUTTON_LEFT, 1, h->x, h->y, 0);
        __android_log_print(ANDROID_LOG_INFO, TAG, "Touch (%d, %d)", h->x, h->y);
    } else if (type == AMOTION_EVENT_ACTION_UP || type == AMOTION_EVENT_ACTION_POINTER_UP) {
        ReleasePointer(h);
    }
    return 1;
}

static void Command(struct android_app* app, int32_t command)
{
    Host* h = app->userData;
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        ANativeWindow_setBuffersGeometry(app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
        h->redraw = 1;
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        h->redraw = 1;
        break;
    case APP_CMD_RESUME: h->active = 1; break;
    case APP_CMD_PAUSE: h->active = 0; break;
    case APP_CMD_GAINED_FOCUS: OpenBGI_Focus(h->engine, 1); break;
    case APP_CMD_LOST_FOCUS:
        ReleasePointer(h);
        OpenBGI_Focus(h->engine, 0);
        break;
    case APP_CMD_TERM_WINDOW:
        ReleasePointer(h);
        h->viewport = (OpenBGI_Rect_t){0};
        break;
    }
}

static void AnswerRequests(OpenBGI_t* engine)
{
    OpenBGI_Request_t q = {.size = sizeof q};
    while (OpenBGI_NextRequest(engine, &q)) {
        int answer = 0;
        if (q.kind == OPENBGI_ASK_MESSAGE) {
            __android_log_print(ANDROID_LOG_INFO, TAG, "%s", q.u.message.text ? q.u.message.text : "");
            unsigned buttons = q.u.message.flags & 15;
            answer = buttons == OPENBGI_MB_RETRYCANCEL ? OPENBGI_IDCANCEL :
                buttons == OPENBGI_MB_YESNO ? OPENBGI_IDNO : OPENBGI_IDOK;
        }
        OpenBGI_Answer(engine, q.id, answer);
    }
}

void android_main(struct android_app* app)
{
    Host h = {.app = app, .pointer = -1};
    OpenBGI_Host_t api = {.size = sizeof api, .api = OPENBGI_API_VERSION, .user = &h,
        .ticksMs = Ticks, .files = &kFiles, .caps = OPENBGI_CAP_DIALOGS};
    h.engine = OpenBGI_New(&api);
    if (!h.engine) {
        ANativeActivity_finish(app->activity);
        return;
    }
    app->userData = &h;
    app->onAppCmd = Command;
    app->onInputEvent = Input;
    int run = OpenBGI_Start(h.engine, DEMO);
    if (run != OPENBGI_RUNNING) ANativeActivity_finish(app->activity);
    OpenBGI_Frame_t frame = {.size = sizeof frame};
    int delay = 0;
    int audioAttempted = 0;
    while (!app->destroyRequested) {
        struct android_poll_source* source = NULL;
        int runnable = run == OPENBGI_RUNNING && app->window && h.active;
        if (ALooper_pollOnce(runnable ? delay : -1, NULL, NULL, (void**)&source) >= 0 && source)
            source->process(app, source);
        if (app->destroyRequested) break;
        if (!h.active || !app->window || run != OPENBGI_RUNNING) {
            if (h.audio) CloseAudio(&h);
            audioAttempted = 0;
            continue;
        }
        if (!audioAttempted) {
            OpenAudio(&h);
            audioAttempted = 1;
        }
        AnswerRequests(h.engine);
        OpenBGI_PumpEvents(h.engine);
        run = OpenBGI_RunFrame(h.engine, &frame);
        if (frame.changes || h.redraw) Present(&h, &frame);
        if (run != OPENBGI_RUNNING) {
            AnswerRequests(h.engine);
            CloseAudio(&h);
            ANativeActivity_finish(app->activity);
        }
        delay = frame.idleMs < 1 ? 1 : frame.idleMs > 16 ? 16 : frame.idleMs;
    }
    CloseAudio(&h);
    int status = OpenBGI_Stop(h.engine);
    AnswerRequests(h.engine);
    OpenBGI_Delete(h.engine);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Engine stopped: %d", status);
}

