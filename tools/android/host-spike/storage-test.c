/* SPDX-License-Identifier: GPL-2.0-only */
#include "audio.h"
#include "fonts.h"
#include "frame.h"
#include "host.h"
#include "requests.h"
#include "../../../src/os/android/storage_paths.h"
#include "../../../src/os/android/audio.h"
#include "../../../src/os/android/audio_output.h"
#include "../../../src/os/android/clock.h"
#include "../../../src/os/posix/posix_internal.h"
#include "../tests/stubs/aaudio/mock.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr, "host storage:%d: %s\n", __LINE__, #x); exit(1); } } while(0)

static const OpenBGI_Files_t* files;
static uint32_t now = 100000;

int TestClockGetTime(clockid_t clock, struct timespec* time)
{
    CHECK(clock == CLOCK_MONOTONIC);
    time->tv_sec = now / 1000;
    time->tv_nsec = (now % 1000) * 1000000;
    return 0;
}
static void LocalTime(void* user, OpenBGI_DateTime_t* out)
{
    (void)user;
    *out = (OpenBGI_DateTime_t){.year = 2026, .month = 1, .day = 1, .dayOfWeek = 4};
}
static void Log(const char* text) { puts(text); }

static void CheckCanvas(void)
{
    uint8_t bgrx[] = {
        0,0,255,0, 0,255,0,64, 255,0,0,0, 0,0,0,0,
        8,16,32,0, 17,18,19,0, 20,30,40,0, 0,0,0,0,
    };
    OpenBGI_Frame_t frame = {.size = sizeof frame, .pixels = bgrx, .width = 3, .height = 2, .pitch = 16, .bpp = 32};
    AndroidDamage_t damage;
    HostCanvas_t canvas = {0};
    CHECK(HostCanvasUpdate(&canvas, &frame, &damage));
    CHECK(damage.r == 3 && damage.b == 2);
    CHECK(canvas.pixels[0] == 0xff0000ff && canvas.pixels[1] == 0xff00ff00 && canvas.pixels[2] == 0xffff0000);
    CHECK(canvas.pixels[3] == 0xff081020 && canvas.pixels[5] == 0xff141e28);
    bgrx[0] = 255;
    bgrx[4] = 255;
    frame.changes = 2;
    frame.change[0] = (OpenBGI_Rect_t){-1,0,2,1};
    frame.change[1] = (OpenBGI_Rect_t){3,0,5,2};
    CHECK(HostCanvasUpdate(&canvas, &frame, &damage));
    CHECK(damage.l == 0 && damage.t == 0 && damage.r == 1 && damage.b == 1);
    CHECK(canvas.pixels[0] == 0xffff00ff && canvas.pixels[1] == 0xff00ff00);
    frame.changes = 0;
    CHECK(HostCanvasUpdate(&canvas, &frame, &damage) && damage.r == 0 && damage.b == 0);
    uint8_t rgb555[] = {0,0xfc, 0xe0,3, 0x1f,0, 0,0, 0xff,0x7f, 0,0, 0,0, 0,0};
    frame.pixels = rgb555;
    frame.pitch = 8;
    frame.bpp = 16;
    CHECK(HostCanvasUpdate(&canvas, &frame, &damage) && damage.r == 3 && damage.b == 2);
    CHECK(canvas.pixels[0] == 0xff0000ff && canvas.pixels[1] == 0xff00ff00 &&
        canvas.pixels[2] == 0xffff0000 && canvas.pixels[3] == 0xffffffff);
    frame.pitch = 5;
    CHECK(!HostCanvasUpdate(&canvas, &frame, &damage) && canvas.bpp == 0);
    free(canvas.pixels);

    Viewport_t viewport = AndroidViewport(1920, 1080, 800, 600, 0);
    int x, y;
    CHECK(!AndroidViewportPoint(viewport, 800, 600, 239.5f, 540, &x, &y));
    CHECK(AndroidViewportPoint(viewport, 800, 600, 960, 540, &x, &y) && x == 400 && y == 300);
    CHECK(!AndroidViewportPoint(viewport, 800, 600, 1680, 540, &x, &y) && x == 800);
}

static void ReadText(const char* path, const char* expected)
{
    char bytes[20];
    void* stream = files->open(NULL, path, OPENBGI_OPEN_READ);
    size_t size = strlen(expected);
    CHECK(stream && files->length(NULL, stream) == size);
    CHECK(!files->seek(NULL, stream, UINT64_C(1) << 32));
    CHECK(files->read(NULL, stream, bytes, sizeof bytes) == size && !memcmp(bytes, expected, size));
    files->close(NULL, stream);
}

static void WriteText(const char* path, const char* text)
{
    void* stream = files->open(NULL, path, OPENBGI_OPEN_CREATE);
    size_t size = strlen(text);
    CHECK(stream && files->write(NULL, stream, text, (uint32_t)size) == size);
    files->close(NULL, stream);
}

/* Compare what the shared renderer receives with the demo's two script colours. */
static uint32_t Colour(int bpp, unsigned r, unsigned g, unsigned b)
{
    if(bpp == 16) {
        r = (r >> 3) * 255 / 31;
        g = (g >> 3) * 255 / 31;
        b = (b >> 3) * 255 / 31;
    }
    return UINT32_C(0xff000000) | b << 16 | g << 8 | r;
}

static void CheckTextFrame(const HostCanvas_t* canvas, uint32_t background, const char* path)
{
    enum {Left = 100, Top = 100, Width = 256, Height = 64};
    FILE* file = fopen(path, "wb");
    CHECK(file);
    int latin = 0, top = Height, bottom = -1;
    for(int y = 0; y < Height; ++y) {
        const uint32_t* row = canvas->pixels + (Top + y) * canvas->width + Left;
        CHECK(fwrite(row, sizeof *row, Width, file) == Width);
        for(int x = 0; x < Width; ++x) {
            if(row[x] == background) continue;
            if(x < 64) ++latin;
            if(x >= 160) {
                if(y < top) top = y;
                bottom = y;
            }
        }
    }
    CHECK(fclose(file) == 0);
    int height = bottom - top + 1;
    CHECK(latin > 50 && height >= 48 && height <= Height);
    printf("HOST_TEXT: 64 px cells, %d px kanji ink height, %d Latin ink pixels\n", height, latin);
}

static void CheckTone(void)
{
    int first = -1, last = -1;
    for(int i = 0; i < mockAudio.capturedFrames; ++i) {
        int sample = mockAudio.captured[i * 2];
        CHECK(sample == mockAudio.captured[i * 2 + 1]);
        if(abs(sample) > 10) {
            if(first < 0) first = i;
            last = i;
        }
    }
    CHECK(first >= 0 && last > first);
    int firstCrossing = -1, lastCrossing = -1, crossings = 0;
    for(int i = first + 1; i <= last; ++i) {
        if(mockAudio.captured[(i - 1) * 2] < 0 && mockAudio.captured[i * 2] >= 0) {
            if(firstCrossing < 0) firstCrossing = i;
            lastCrossing = i;
            ++crossings;
        }
    }
    CHECK(crossings > 1);
    int length = last - first + 1;
    double duration = 1000.0 * length / mockAudio.nativeRate;
    double pitch = (double)mockAudio.nativeRate * (crossings - 1) / (lastCrossing - firstCrossing);
    CHECK(duration >= 247.0 && duration <= 252.0 && pitch >= 439.0 && pitch <= 441.0);
    printf("HOST_TONE: %.1f ms, %.1f Hz, %d Hz output\n", duration, pitch, mockAudio.nativeRate);
}

static void WaitForAudio(HostAudio_t* audio, OpenBGI_t* engine, int requireRetry)
{
    int failed = 0;
    for(int i = 0; i < 450; ++i) {
        HostAudioUpdate(audio, engine, 1);
        int status = OsAndroid_AudioStatus();
        if(status < 0) failed = 1;
        if(status > 0 && (!requireRetry || failed) && __atomic_load_n(&audio->reading, __ATOMIC_RELAXED))
            return;
        struct timespec interval = {0, 10000000};
        nanosleep(&interval, NULL);
        now += 10;
        OpenBGI_RunAudio(engine);
    }
    CHECK(0 && "audio worker did not reach an open stream");
}

static void CheckAudioWorker(OpenBGI_t* engine)
{
    MockAudioReset();
    mockAudio.writePauseNs = 20000000;
    mockAudio.writeFailures = 1;
    HostAudio_t audio = {0};
    WaitForAudio(&audio, engine, 1);
    HostAudioUpdate(&audio, engine, 0);
    CHECK(mockAudio.opens == 2 && mockAudio.streams == 0 && OsAndroid_AudioStatus() == 0);
    WaitForAudio(&audio, engine, 0);
    HostAudioUpdate(&audio, engine, 0);
    CHECK(mockAudio.opens == 3 && mockAudio.streams == 0 && mockAudio.capturedFrames > 0);
}

typedef struct TestMessage {
    int choice, shown, polled;
} TestMessage_t;

static int ShowTestMessage(void* ui, const OpenBGI_Request_t* request, int positive, int negative)
{
    TestMessage_t* message = ui;
    CHECK(++message->shown == 1);
    if(message->choice) {
        CHECK(positive == OPENBGI_IDYES && negative == OPENBGI_IDNO);
        CHECK(request->u.message.flags & OPENBGI_MB_DEFBUTTON2);
        char utf8[128];
        OS_SjisToUtf8(request->u.message.text, utf8, sizeof utf8);
        CHECK(!strcmp(utf8, "テストを開始しますか？"));
    } else {
        CHECK(positive == OPENBGI_IDOK && negative == 0);
        CHECK(!strcmp(request->u.message.text, "OpenBGI host spike ready"));
    }
    return 1;
}

static int PollTestMessage(void* ui)
{
    TestMessage_t* message = ui;
    return ++message->polled < 5 ? 0 : message->choice ? message->choice : OPENBGI_IDOK;
}

static void PauseForFiveSeconds(const OpenBGI_Host_t* host)
{
    uint32_t ticks = host->ticksMs(host->user);
    int64_t before, after;
    CHECK(host->perfNs(host->user, &before));
    OsAndroid_ClockSetResumed(0);
    now += 2500;
    OsAndroid_ClockSetResumed(0);
    now += 2500;
    CHECK(host->perfNs(host->user, &after) && after == before);
    CHECK(host->ticksMs(host->user) == ticks);
    OsAndroid_ClockSetResumed(1);
    CHECK(host->perfNs(host->user, &after) && after == before);
    CHECK(host->ticksMs(host->user) == ticks);
}

static void Boot(const char* args, int choice, const char* textFrame, int pause)
{
    OpenBGI_Host_t host = AndroidHostConfig();
    host.localTime = LocalTime;
    host.caps = OPENBGI_CAP_MESSAGES;
    OpenBGI_t* engine = OpenBGI_New(&host);
    CHECK(engine);
    int run = OpenBGI_Start(engine, args);
    OpenBGI_Focus(engine, 1);
    MockAudioReset();
    mockAudio.rejectRate = 1;
    AndroidAudioOutput_t output = {0};
    enum {Period = OPENBGI_AUDIO_RATE / 50};
    int16_t samples[Period * 2];
    CHECK(AndroidAudio_Open(&output, OPENBGI_AUDIO_RATE, Period, 1) &&
        output.rate == 48000 && mockAudio.opens == 2);
    HostRequests_t requests = {0};
    TestMessage_t message = {.choice = choice};
    const HostMessageUi_t ui = {.show = ShowTestMessage, .result = PollTestMessage, .ui = &message};
    int stage = 0, tail = 0;
    int paused = 0;
    uint32_t greenAt = 0;
    HostCanvas_t canvas = {0};
    for(int i = 0; i < 100 && run == OPENBGI_RUNNING && tail < 20; ++i) {
        if(pause && stage == 2 && !paused && host.ticksMs(host.user) - greenAt >= 60) {
            PauseForFiveSeconds(&host);
            paused = 1;
        }
        int waiting = HostRequestsPump(&requests, engine, &ui);
        if(waiting) OpenBGI_RunAudio(engine);
        OpenBGI_Frame_t frame = {.size = sizeof frame};
        OpenBGI_PumpEvents(engine);
        run = OpenBGI_RunFrame(engine, &frame);
        int frames = OpenBGI_AudioRead(engine, samples, Period);
        if(frames) CHECK(AndroidAudio_Write(&output, samples, frames));
        AndroidDamage_t damage;
        if(stage < 3 && message.shown && frame.expose == OPENBGI_EXPOSE_PICTURE && frame.width == 800 && frame.height == 600 &&
            HostCanvasUpdate(&canvas, &frame, &damage)) {
            uint32_t pixel = canvas.pixels[300 * 800 + 400];
            uint32_t expected = stage == 1 ? Colour(frame.bpp, 0x38, 0x78, 0x58) : Colour(frame.bpp, 0x24, 0x55, 0x84);
            if(pixel == expected) {
                if(stage == 0 && textFrame) CheckTextFrame(&canvas, expected, textFrame);
                if(choice) CHECK(!waiting);
                if(stage == 1) greenAt = host.ticksMs(host.user);
                if(stage == 2 && pause) {
                    uint32_t elapsed = host.ticksMs(host.user) - greenAt;
                    CHECK(paused && elapsed >= 280 && elapsed <= 340);
                    printf("HOST_PAUSE: 5000 ms suspended, script wait completed at %u ms of game time\n", elapsed);
                }
                if(stage < 2) {
                    int x, y;
                    CHECK(AndroidViewportPoint(AndroidViewport(1920, 1080, 800, 600, 0), 800, 600, 960, 540, &x, &y));
                    OpenBGI_Pointer(engine, x, y);
                    OpenBGI_Button(engine, OPENBGI_BUTTON_LEFT, stage == 0, x, y, 0);
                }
                ++stage;
            }
        }
        if(stage == 3) ++tail;
        now += 20;
    }
    CHECK(message.shown == 1 && message.polled == 5 && requests.positive == 0);
    free(canvas.pixels);
    AndroidAudio_Close(&output);
    if(choice == OPENBGI_IDNO) {
        CHECK(stage == 0 && run == OPENBGI_DONE);
        puts("HOST_MESSAGE: No exits without starting the scene");
    } else {
        CHECK(stage == 3 && run == OPENBGI_RUNNING);
        if(choice) puts("HOST_MESSAGE: Yes resumes the script after waiting for an answer");
        CheckTone();
        CheckAudioWorker(engine);
    }
    CHECK(OpenBGI_Stop(engine) == 0);
    OpenBGI_Delete(engine);
    AndroidHostFontsClose();
}

int main(int argc, char** argv)
{
    CHECK(argc >= 2 && argc <= 4);
    files = AndroidHostConfig().files;
    CheckCanvas();
    CHECK(OsAndroid_InitStorage(argv[1], NULL, Log));
    AndroidGameSettings_t settings;
    OsAndroid_ReadGameSettings(&settings);
    char args[96];
    CHECK(!AndroidHostGameOptions(&settings, args, sizeof args));
    printf("HOST_OPTIONS: %s, archive %s, scale %d\n", args, settings.archiveLanguage, settings.scale);
    ReadText("C:\\Events.arc", "traditional");
    ReadText("C:\\override.txt", "patch");

    CHECK(files->makeDir(NULL, "C:\\UserData"));
    WriteText("C:\\UserData\\slot.sud", "saved");
    ReadText("C:\\UserData\\slot.sud", "saved");
    void* update = files->open(NULL, "C:\\override.txt", OPENBGI_OPEN_WRITE);
    CHECK(update && files->length(NULL, update) == 5);
    CHECK(files->write(NULL, update, "P", 1) == 1);
    files->close(NULL, update);
    ReadText("C:\\override.txt", "patch"); // patch still has precedence over the writable layer

    AndroidHostTextEncoding(NULL, 1);
    CHECK(files->makeDir(NULL, "C:\\日本語"));
    WriteText("C:\\日本語\\名前.txt", "utf8");
    OpenBGI_FindData_t entry;
    void* find = files->findFirst(NULL, "C:\\日本語\\*.txt", &entry);
    CHECK(find && !strcmp(entry.name, "名前.txt"));
    files->findClose(NULL, find);
    AndroidHostTextEncoding(NULL, 0);
    char sjis[100];
    OS_Utf8ToSjis("C:\\日本語\\名前.txt", sjis, sizeof sjis);
    ReadText(sjis, "utf8");

    int choice = argc >= 3 ? atoi(argv[2]) : 0;
    int pause = argc == 4 && !strcmp(argv[3], "--pause");
    Boot(args, choice, argc == 4 && !pause ? argv[3] : NULL, pause);
    ReadText("C:\\UserData\\slot.sud", "saved");
    puts(choice == OPENBGI_IDNO ? "HOST_PASS: layered storage and declined confirmation" :
        "HOST_PASS: layered storage, pixel conversion and blue/green/blue at 800x600");
    return 0;
}
