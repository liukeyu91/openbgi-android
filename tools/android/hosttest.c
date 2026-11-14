/* SPDX-License-Identifier: GPL-2.0-only
 * Opt-in test APK only. --wrap=Engine_Main exercises the real Android OS
 * services without game data, saves, or test hooks in the released library.
 */
#include "bgi/os.h"
#include "bgi/strutil.h"
#include "../../src/os/android/android_internal.h"
#include "../../src/os/android/renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static int gFailed;
static int gObservedPause, gFrozen;
static void Check(const char* label, int ok)
{
    char line[256];
    snprintf(line, sizeof line, "HOSTTEST %s %s", ok ? "PASS" : "FAIL", label);
    OsAndroid_Log(line);
    if(!ok) ++gFailed;
}

static void* ObservePause(void* unused)
{
    uint32_t ticks;
    int64_t perf, after;
    BGI_UNUSED(unused);
    pthread_mutex_lock(&gAndroidHost->lock);
    while(gAndroidHost->resumed && !gAndroidHost->destroyed)
        pthread_cond_wait(&gAndroidHost->changed, &gAndroidHost->lock);
    ticks = OS_TicksMs(); OS_PerfCounterNs(&perf);
    pthread_mutex_unlock(&gAndroidHost->lock);
    OS_SleepMs(1000);
    OS_PerfCounterNs(&after);
    pthread_mutex_lock(&gAndroidHost->lock);
    gFrozen = OS_TicksMs() == ticks && after == perf && !gAndroidHost->resumed;
    gObservedPause = 1;
    pthread_mutex_unlock(&gAndroidHost->lock);
    return NULL;
}

static void PauseTest(void)
{
    pthread_t observer;
    OS_MessageBox("After OK: press Home, wait 3 seconds, return to this app.", "Lifecycle test", OS_MB_OK);
    pthread_create(&observer, NULL, ObservePause, NULL);
    OsAndroid_Log("HOSTTEST PAUSE READY");
    for(;;)
    {
        int done;
        OS_PumpMessages();
        pthread_mutex_lock(&gAndroidHost->lock);
        done = (gObservedPause && gAndroidHost->resumed) || gAndroidHost->destroyed;
        pthread_mutex_unlock(&gAndroidHost->lock);
        if(done) break;
        OS_SleepMs(5);
    }
    pthread_join(observer, NULL);
    Check("background clocks frozen", gFrozen);
}

static void Entry(const char* label, int encoding, const char* initial, int limit, const char* expected)
{
    char actual[1024];
    /* Drain the previous fixture's queued key release before showing a field. */
    while(OS_PumpMessages() || OS_KeyState(13))OS_SleepMs(5);
    OsAndroid_Log(label);
    Text_SetEncoding(encoding);
    OS_EditMove(240, 240, 800, 80);
    OS_EditSetFont("sans-serif", 32, 16);
    OS_EditSetColour(0xeeeeee);
    OS_EditLimitText(limit);
    OS_EditSetText(initial);
    OS_EditShow(1); OS_EditFocus(1);
    for(;;)
    {
        OS_PumpMessages();
        if(OS_KeyState(13)) break;
        pthread_mutex_lock(&gAndroidHost->lock);
        int destroyed = gAndroidHost->destroyed;
        pthread_mutex_unlock(&gAndroidHost->lock);
        if(destroyed) return;
        OS_SleepMs(5);
    }
    OS_EditFocus(0);
    OS_EditGetText(actual, sizeof actual);
    Check(label, strcmp(actual, expected) == 0);
    OS_EditShow(0);
    OS_MessageBox(strcmp(actual, expected) == 0 ? "PASS" : "FAIL", label, OS_MB_OK);
}

static void FormTests(void)
{
    char a[256] = "unchanged", b[256] = "unchanged";
    char surname[11] = "\xd6\xd0", given[11] = "\xce\xc4", nickname[11] = "\xb2\xe2", pronoun[11] = "\xca\xd4";
    int32_t month = 1, day = 30;
    Text_SetEncoding(TEXT_GBK);
    Check("input1 cancel preserves output", !OS_DialogInput1("Form 1: choose CANCEL", "Seed", 6, 0, a) && !strcmp(a, "unchanged"));
    Check("input1 bytes", OS_DialogInput1("Form 2: replace with FormABCD", "Seed", 6, 0, a) && !strcmp(a, "FormAB"));
    Check("input2 fields", OS_DialogInput2("Form 3: choose OK", "Left", "A", 10, 0, a, "Right", "B", 10, 0, b, 1) && !strcmp(a, "A") && !strcmp(b, "B"));
    Check("profile and February days", OS_DialogProfile(surname, given, nickname, pronoun, &month, &day) &&
        !strcmp(surname, "\xd6\xd0") && !strcmp(given, "\xce\xc4") && !strcmp(nickname, "\xb2\xe2") && !strcmp(pronoun, "\xca\xd4") && month == 1 && day == 0);
}

/* Mixed-encoding defaults exercise production EditText/JNI, not a mock UI.
 * Drive with a long press and the explicit SJIS candidate, then normal Done/OK.
 * Run only in the isolated host-test APK by creating repair-only in its game. */
static void RepairTests(void)
{
    char a[256] = "unchanged", b[256] = "unchanged";
    char surname[11] = "\x90\xc2\x97\x74", given[11] = "\x91\xbe\x98\x59";
    char nickname[11] = "\x89\xd4\x8e\x71", pronoun[11] = "\x8e\x84";
    int32_t month = 1, day = 2;
    Entry("REPAIR edit: cancel preview, reopen SJIS, Done", TEXT_GBK,
        "\x90\xc2\x97\x74", 10, "\xc7\xe0\xc8\x7e");
    Entry("REPAIR background: Home while preview, return, SJIS, Done", TEXT_GBK,
        "\x91\xbe\x98\x59", 10, "\xcc\xab\xc0\xc9");
    OS_EditDestroy();
    Text_SetEncoding(TEXT_GBK);
    Check("repair input1 cancel preserves output",
        !OS_DialogInput1("REPAIR form cancel: SJIS then cancel parent", "\x90\xc2\x97\x74", 10, 0, a) && !strcmp(a,"unchanged"));
    Check("repair input1 GBK bytes",
        OS_DialogInput1("REPAIR form OK: SJIS then OK", "\x90\xc2\x97\x74", 10, 0, a) && !strcmp(a,"\xc7\xe0\xc8\x7e"));
    Check("repair input2 isolates fields",
        OS_DialogInput2("REPAIR two fields: fix Left only", "Left", "\x90\xc2\x97\x74", 10, 0, a,
            "Right", "\xd6\xd0\xce\xc4", 10, 0, b, 1) && !strcmp(a,"\xc7\xe0\xc8\x7e") && !strcmp(b,"\xd6\xd0\xce\xc4"));
    Check("repair profile four fields and birthday",
        OS_DialogProfile(surname,given,nickname,pronoun,&month,&day) &&
        !strcmp(surname,"\xc7\xe0\xc8\x7e") && !strcmp(given,"\xcc\xab\xc0\xc9") &&
        !strcmp(nickname,"\xbb\xa8\xd7\xd3") && !strcmp(pronoun,"\xcb\xbd") && month==1 && day==2);
}

static void RepairGuardTests(void)
{
    char actual[128];
    Text_SetEncoding(TEXT_GBK);
    OS_EditMove(240,240,800,80);OS_EditSetFont("sans-serif",32,16);
    OS_EditSetColour(0xeeeeee);OS_EditLimitText(10);
    OS_EditSetText("\x90\xc2\x97\x74");OS_EditShow(1);OS_EditFocus(1);
    OsAndroid_Log("REPAIR GUARD READY: open preview then create repair-update-go");
    while(OS_FileAttrs("repair-update-go")==OS_INVALID_ATTRS){OS_PumpMessages();OS_SleepMs(10);}
    OS_FileDelete("repair-update-go");
    OS_EditSetText("\x91\xbe\x98\x59");
    OsAndroid_Log("REPAIR GUARD UPDATED: old preview must close; repair new SJIS default");
    for(;;){OS_PumpMessages();if(OS_KeyState(13))break;OS_SleepMs(5);}
    OS_EditFocus(0);OS_EditGetText(actual,sizeof actual);
    Check("repair script update invalidates old preview",!strcmp(actual,"\xcc\xab\xc0\xc9"));
    OS_EditShow(0);OS_EditDestroy();
    Entry("REPAIR limit: SJIS exceeds UTF8 limit; cancel, type AB",TEXT_UTF8,"\x90\xc2\x97\x74",5,"AB");
    Entry("REPAIR unrepresentable: UTF8 emoji to GBK; cancel, type AB",TEXT_GBK,"\xf0\x9f\x98\x80",10,"AB");
    OS_EditDestroy();
}

static int gInputDone;
static pthread_t gInputOwner;
static void InputButton(int button,int down,int x,int y)
{
    char line[160];
    snprintf(line,sizeof line,"HOSTINPUT button %d %d at %d %d state %d thread %d",button,down,x,y,
        OS_KeyState(button+1),pthread_equal(pthread_self(),gInputOwner));OsAndroid_Log(line);
}
static void InputKeyDown(int vk,int repeat)
{
    char line[160];snprintf(line,sizeof line,"HOSTINPUT key %d down repeat %d state %d thread %d",vk,repeat,
        OS_KeyState(vk),pthread_equal(pthread_self(),gInputOwner));OsAndroid_Log(line);
    if(vk==0x7b)gInputDone=1;
}
static void InputKeyUp(int vk)
{
    char line[160];snprintf(line,sizeof line,"HOSTINPUT key %d up state %d",vk,OS_KeyState(vk));OsAndroid_Log(line);
}
static void InputWheel(int delta)
{
    char line[80];snprintf(line,sizeof line,"HOSTINPUT wheel %d",delta);OsAndroid_Log(line);
}
static void InputTest(void)
{
    uint32_t* pixels=calloc(1280u*720,sizeof *pixels);
    OsSurface_t picture={pixels,1280*4,1280,720,32};
    OsEventHandlers_t events={0};int x,y;
    events.key_down=InputKeyDown;events.key_up=InputKeyUp;events.mouse_button=InputButton;events.mouse_wheel=InputWheel;
    gInputOwner=pthread_self();OS_SetEventHandlers(&events);
    for(y=0;y<720;y++)for(x=0;x<1280;x++)pixels[y*1280+x]=
        (x>=630 && x<650)||(y>=350 && y<370)?0x00ffffffu:0x00204080u;
    OS_BlitToWindow(NULL,0,0,1280,720,&picture,0,0);free(pixels);
    OsAndroid_Log("HOSTINPUT READY: cross at logical 640,360; F12 ends test");
    while(!gInputDone){
        int destroyed;OS_PumpMessages();
        pthread_mutex_lock(&gAndroidHost->lock);destroyed=gAndroidHost->destroyed;pthread_mutex_unlock(&gAndroidHost->lock);
        if(destroyed)break;OS_SleepMs(5);
    }
    OS_WindowDestroy();
}

/* Read back the bound canvas texture, independent of swap-buffer preservation. */
static uint32_t RenderPixel(int x,int y)
{
    GLint texture;GLuint fb;uint32_t pixel=0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGenFramebuffers(1,&fb);
    glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,(GLuint)texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE)
        glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,&pixel);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fb);
    return pixel;
}
static int RenderWait(uint64_t previous)
{
    uint32_t start=OS_TicksMs();
    while(OsAndroid_RenderStats().frames<=previous && OS_TicksMs()-start<2000){OS_PumpMessages();OS_SleepMs(1);}
    return OsAndroid_RenderStats().frames>previous;
}
static void RenderTest(void)
{
    uint32_t* pixels=malloc(1280u*720*4);
    OsSurface_t picture={pixels,1280*4,1280,720,32};
    AndroidRenderStats_t before,after;
    int x,y;uint32_t start;
    for(y=0;y<720;y++)for(x=0;x<1280;x++)pixels[y*1280+x]=
        y<360?(x<640?0x00ff0000:0x0000ff00):(x<640?0x000000ff:0x00ffffff);
    before=OsAndroid_RenderStats();OS_BlitToWindow(NULL,0,0,1280,720,&picture,0,0);
    Check("EGL initial frame",RenderWait(before.frames));
    Check("EGL RGB channels and row order",RenderPixel(10,10)==0xff0000ffu && RenderPixel(900,10)==0xff00ff00u &&
        RenderPixel(10,500)==0xffff0000u && RenderPixel(900,500)==0xffffffffu);
    OsAndroid_Log("HOSTRENDER QUADRANTS: top red/green, bottom blue/white; screenshot now");
    start=OS_TicksMs();while(OS_TicksMs()-start<2500){OS_PumpMessages();OS_SleepMs(2);}
    {
        uint32_t patch[70];OsSurface_t small={patch,10*4,10,7,32};
        for(x=0;x<70;x++)patch[x]=0x00123456;
        before=OsAndroid_RenderStats();OS_BlitToWindow(NULL,31,47,10,7,&small,0,0);
        Check("EGL partial frame",RenderWait(before.frames));after=OsAndroid_RenderStats();
        Check("EGL narrow upload bytes",after.bytes-before.bytes==10*7*4 && after.partial==before.partial+1 && after.full==before.full);
        Check("EGL narrow upload keeps neighbors",RenderPixel(31,47)==0xff563412u && RenderPixel(40,53)==0xff563412u && RenderPixel(30,47)==0xff0000ffu && RenderPixel(41,53)==0xff0000ffu);
        before=after;OS_BlitToWindow(NULL,-4,-3,10,7,&small,0,0);RenderWait(before.frames);
        Check("EGL clipped upload",RenderPixel(0,0)==0xff563412u && RenderPixel(5,3)==0xff563412u && RenderPixel(6,3)==0xff0000ffu);
    }
    {
        uint16_t rgb[]={0x7c00,0x03e0,0x001f,0x7fff,0,0x7c00};OsSurface_t small={rgb,6,3,2,16};
        before=OsAndroid_RenderStats();OS_BlitToWindow(NULL,200,100,3,2,&small,0,0);RenderWait(before.frames);
        Check("EGL 16-bit conversion",RenderPixel(200,100)==0xff0000ffu && RenderPixel(201,100)==0xff00ff00u && RenderPixel(202,100)==0xffff0000u && RenderPixel(200,101)==0xffffffffu);
    }
    {
        uint32_t rgb[]={0xff0000,0x00ff00,0x0000ff,0xffffff};int clip[]={502,201,507,205};OsSurface_t small={rgb,8,2,2,32};
        before=OsAndroid_RenderStats();OS_StretchToWindow(NULL,500,200,8,6,&small,clip);RenderWait(before.frames);
        Check("EGL stretched clipped blit",RenderPixel(502,201)==0xff0000ffu && RenderPixel(506,204)==0xffffffffu && RenderPixel(507,204)==0xff0000ffu);
    }
    before=OsAndroid_RenderStats();OS_WindowFillBlack();RenderWait(before.frames);
    Check("EGL black fill uploads whole canvas",RenderPixel(31,47)==0 && RenderPixel(900,500)==0);
    before=OsAndroid_RenderStats();OS_WindowSetPicture(640,360);OS_BlitToWindow(NULL,0,0,640,360,&picture,640,0);RenderWait(before.frames);
    Check("EGL canvas resize",OsAndroid_RenderStats().full==before.full+1 && RenderPixel(600,300)==0xff00ff00u);
    before=OsAndroid_RenderStats();OS_WindowSetPicture(1280,720);OS_BlitToWindow(NULL,0,0,1280,720,&picture,0,0);RenderWait(before.frames);
    Check("EGL canvas restore",RenderPixel(10,500)==0xffff0000u);
    before=OsAndroid_RenderStats();start=OS_TicksMs();
    while(OS_TicksMs()-start<150){OS_PumpMessages();OS_SleepMs(1);}
    Check("EGL static frame does not swap",OsAndroid_RenderStats().frames==before.frames);
    {
        uint64_t ticksBefore,ticksAfter;
        pthread_mutex_lock(&gAndroidHost->lock);ticksBefore=gAndroidHost->frameSerial;pthread_mutex_unlock(&gAndroidHost->lock);
        start=OS_TicksMs();before=OsAndroid_RenderStats();
        while(OS_TicksMs()-start<250){OS_BlitToWindow(NULL,3,3,10,7,&picture,0,0);OS_PumpMessages();}
        after=OsAndroid_RenderStats();
        pthread_mutex_lock(&gAndroidHost->lock);ticksAfter=gAndroidHost->frameSerial;pthread_mutex_unlock(&gAndroidHost->lock);
        Check("EGL swaps limited by vsync tokens",after.frames>1 && after.frames-before.frames<=ticksAfter-ticksBefore+1 && after.frames-before.frames<40);
        {char line[160];snprintf(line,sizeof line,"HOSTRENDER 250ms: swaps=%llu ticks=%llu",(unsigned long long)(after.frames-before.frames),(unsigned long long)(ticksAfter-ticksBefore));OsAndroid_Log(line);}
    }
    before=OsAndroid_RenderStats();PauseTest();
    /* onResume can precede surfaceChanged: await a NEW post-resume frame. */
    after=OsAndroid_RenderStats();OS_BlitToWindow(NULL,0,0,1,1,&picture,0,0);RenderWait(after.frames);
    Check("EGL texture restored after pause",RenderPixel(10,500)==0xffff0000u && OsAndroid_RenderStats().full>before.full);
    Check("EGL no driver errors",OsAndroid_RenderStats().errors==0 && glGetError()==GL_NO_ERROR);
    free(pixels);
}

static void EncodingTest(void)
{
    char out[128];
    Text_SetEncoding(TEXT_GBK);
    OsAndroid_ScriptToUtf8("\xd6\xd0\xce\xc4" "AB",out,sizeof out);
    Check("script GBK body",!strcmp(out,"\xe4\xb8\xad\xe6\x96\x87" "AB"));
    OsAndroid_ScriptToUtf8("\xd6\xd0\xce\xc4" "AB",out,7);
    Check("script GBK bounded whole characters",!strcmp(out,"\xe4\xb8\xad\xe6\x96\x87"));
    OsAndroid_ScriptToUtf8("\xd6\xd0",out,3);Check("script short buffer",!out[0]);
    out[0]='Q';OsAndroid_ScriptToUtf8("\xd6\xd0",out,0);Check("script zero capacity",out[0]=='Q');
    OsAndroid_ScriptToUtf8("\x80",out,sizeof out);Check("script GBK euro",!strcmp(out,"\xe2\x82\xac"));
    OS_SjisToUtf8("\x93\xfa\x96\x7b",out,sizeof out);
    Check("CP932 engine text stays CP932 in GBK mode",!strcmp(out,"\xe6\x97\xa5\xe6\x9c\xac"));
    OS_WindowSetTitle("\xd6\xd0\xce\xc4\xb1\xea\xcc\xe2");
    OS_MessageBox("\xd6\xd0\xce\xc4" "AB","\x93\xfa\x96\x7b (script)",OS_MB_OK);
    OS_MessageBox("\x93\xfa\x96\x7b","CP932 engine body",OS_MB_OK);
    Text_SetEncoding(TEXT_SJIS);OsAndroid_ScriptToUtf8("\x93\xfa\x96\x7b",out,sizeof out);
    Check("script SJIS body",!strcmp(out,"\xe6\x97\xa5\xe6\x9c\xac"));
    Text_SetEncoding(TEXT_UTF8);OsAndroid_ScriptToUtf8("\xe4\xb8\xad\xf0\x9f\x98\x80",out,sizeof out);
    Check("script UTF8 and supplementary character",!strcmp(out,"\xe4\xb8\xad\xf0\x9f\x98\x80"));
    out[0]='Q';OsAndroid_ScriptToUtf8("ABC",out,0);Check("script UTF8 zero capacity",out[0]=='Q');
}

int __wrap_Engine_Main(const char* options)
{
    OsWindowDesc_t desc = {"OpenBGI Android Host Tests", 0, 0, 1280, 720};
    BGI_UNUSED(options);
    OS_WindowCreate(&desc); OS_WindowShow(1);
    if(OS_FileAttrs("crash-segv") != OS_INVALID_ATTRS){volatile int* invalid=(volatile int*)(uintptr_t)1;*invalid=7;}
    if(OS_FileAttrs("crash-abrt") != OS_INVALID_ATTRS)abort();
    if(OS_FileAttrs("repair-guards-only") != OS_INVALID_ATTRS){RepairGuardTests();goto done;}
    if(OS_FileAttrs("repair-only") != OS_INVALID_ATTRS){RepairTests();goto done;}
    if(OS_FileAttrs("encoding-only") != OS_INVALID_ATTRS){EncodingTest();goto done;}
    if(OS_FileAttrs("render-only") != OS_INVALID_ATTRS){RenderTest();goto done;}
    if(OS_FileAttrs("input-only") != OS_INVALID_ATTRS){InputTest();return 0;}
    if(OS_FileAttrs("forms-only") != OS_INVALID_ATTRS) { FormTests(); goto done; }
    Check("YESNO/no", OS_MessageBox("Choose NO", "Host test 1", OS_MB_YESNO | OS_MB_DEFBUTTON2) == OS_IDNO);
    Check("YESNO/yes", OS_MessageBox("Choose YES", "Host test 2", OS_MB_YESNO) == OS_IDYES);
    Check("OKCANCEL/cancel", OS_MessageBox("Choose CANCEL", "Host test 3", OS_MB_OKCANCEL) == OS_IDCANCEL);
    Check("RETRYCANCEL/retry", OS_MessageBox("Choose RETRY", "Host test 4", OS_MB_RETRYCANCEL) == OS_IDRETRY);
    Entry("ASCII limit: type TestABCD, Done", TEXT_GBK, "", 6, "TestAB");
    Entry("GBK: enter zhongwen -> Chinese + AB", TEXT_GBK, "", 6, "\xd6\xd0\xce\xc4" "AB");
    Entry("SJIS: replace final B with B, Done", TEXT_SJIS, "\x93\xfa\x96\x7b" "AB", 6, "\x93\xfa\x96\x7b" "AB");
    Entry("UTF8: replace final B with B, Done", TEXT_UTF8, "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80" "AB", 12, "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80" "AB");
    OS_EditDestroy();
    PauseTest();
    FormTests();
done:
    OsAndroid_Log(gFailed ? "HOSTTEST FAILED" : "HOSTTEST ALL PASSED");
    OS_MessageBox(gFailed ? "FAILED: see engine-current.txt" : "ALL PASSED", "Android host tests", OS_MB_OK);
    OS_WindowDestroy();
    return gFailed;
}
