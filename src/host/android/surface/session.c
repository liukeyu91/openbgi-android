/* SPDX-License-Identifier: GPL-2.0-only */
#include "../audio.h"
#include "../frame.h"
#include "../host.h"
#include "../requests.h"
#include "../../../os/android/android_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    OpenBGI_t* engine;
    HostAudio_t audio;
    HostCanvas_t canvas;
    uint64_t frame, retryAt;
    unsigned generation;
    int focused, expose, x, y;
    uint8_t keys[256], buttons[2];
} Session;

static void ResetInput(Session* s)
{
    for(int vk=1;vk<256;++vk)
        if(s->keys[vk]) OpenBGI_Key(s->engine,vk,0,0);
    for(int button=0;button<2;++button)
        if(s->buttons[button]) OpenBGI_Button(s->engine,button,0,s->x,s->y,0);
    memset(s->keys,0,sizeof s->keys);
    memset(s->buttons,0,sizeof s->buttons);
}

static void Input(Session* s,const AndroidInputEvent_t* event,int scale)
{
    if(event->type==AINPUT_EVENT_TYPE_KEY) {
        int key=event->key, down=event->action==AKEY_EVENT_ACTION_DOWN;
        if(key<=0 || key>=256) return;
        s->keys[key]=(uint8_t)down;
        OpenBGI_Key(s->engine,key,down,down && event->repeat ? OPENBGI_KEY_REPEAT : 0);
        return;
    }
    if(event->action==ANDROID_POINTER_CANCEL) {
        for(int button=0;button<2;++button) if(s->buttons[button]) {
            OpenBGI_Button(s->engine,button,0,s->x,s->y,0);
            s->buttons[button]=0;
        }
        return;
    }
    int w=s->canvas.width,h=s->canvas.height,x,y;
    if(w<=0 || h<=0 || event->width<=0 || event->height<=0) return;
    Viewport_t viewport=AndroidViewport(event->width,event->height,w,h,scale);
    int inside=AndroidViewportPoint(viewport,w,h,event->x,event->y,&x,&y);
    if(inside || s->buttons[0] || s->buttons[1]) {
        s->x=x<0?0:x>=w?w-1:x;
        s->y=y<0?0:y>=h?h-1:y;
        OpenBGI_Pointer(s->engine,s->x,s->y);
    }
    int button=event->key;
    if(event->action==ANDROID_POINTER_WHEEL && inside)
        OpenBGI_Wheel(s->engine,(int)(120*event->wheel));
    if(button<0 || button>1) return;
    if(event->action==ANDROID_POINTER_DOWN && inside && !s->buttons[button]) {
        s->buttons[button]=1;
        OpenBGI_Button(s->engine,button,1,s->x,s->y,0);
    } else if(event->action==ANDROID_POINTER_UP && s->buttons[button]) {
        s->buttons[button]=0;
        OpenBGI_Button(s->engine,button,0,s->x,s->y,0);
    }
}

static ANativeWindow* NextFrame(AndroidHost_t* host,Session* s)
{
    for(;;) {
        pthread_mutex_lock(&host->lock);
        if(host->destroyed) { pthread_mutex_unlock(&host->lock); return NULL; }
        if(!host->resumed || !host->window) {
            pthread_mutex_unlock(&host->lock);
            ResetInput(s);
            if(s->focused) OpenBGI_Focus(s->engine,0);
            s->focused=0;
            HostAudioUpdate(&s->audio,s->engine,0);
            OsAndroid_RenderClose();
            s->expose=-1;
            pthread_mutex_lock(&host->lock);
            while(!host->destroyed && (!host->resumed || !host->window))
                pthread_cond_wait(&host->changed,&host->lock);
            pthread_mutex_unlock(&host->lock);
            continue;
        }
        if(host->frameSerial==s->frame) {
            pthread_cond_wait(&host->changed,&host->lock);
            pthread_mutex_unlock(&host->lock);
            continue;
        }
        s->frame=host->frameSerial;
        if(s->generation!=host->redraw) {
            s->generation=host->redraw;
            s->expose=-1;
            s->retryAt=0;
        }
        ANativeWindow* window=host->window;
        ANativeWindow_acquire(window);
        AndroidInputEvent_t events[ANDROID_INPUT_CAPACITY];
        unsigned count=host->inputCount;
        memcpy(events,host->input,count*sizeof *events);
        host->inputCount=0;
        int reset=host->inputReset, focused=host->focused;
        host->inputReset=0;
        pthread_mutex_unlock(&host->lock);

        if(reset || !focused) ResetInput(s);
        if(s->focused!=focused) OpenBGI_Focus(s->engine,focused);
        s->focused=focused;
        if(focused) for(unsigned i=0;i<count;++i) Input(s,&events[i],host->scaleMode);
        return window;
    }
}

static void Present(Session* s,ANativeWindow* window,const OpenBGI_Frame_t* frame,int scale)
{
    static const uint32_t black=UINT32_C(0xff000000);
    AndroidDamage_t damage={0};
    const uint32_t* pixels=&black;
    int w=1,h=1,expose=OPENBGI_EXPOSE_BLACK;
    if(frame->expose==OPENBGI_EXPOSE_NONE) { s->canvas.bpp=0; return; }
    int oldW=s->canvas.width,oldH=s->canvas.height;
    if(frame->expose==OPENBGI_EXPOSE_PICTURE && HostCanvasUpdate(&s->canvas,frame,&damage)) {
        pixels=s->canvas.pixels;
        w=s->canvas.width;
        h=s->canvas.height;
        expose=OPENBGI_EXPOSE_PICTURE;
        if(w!=oldW || h!=oldH) OsAndroid_EditCommand(ANDROID_EDIT_CANVAS,w,h,0,0,NULL);
    } else s->canvas.bpp=0;
    if(s->expose==expose && damage.l==damage.r) return;
    uint64_t now=OsAndroid_MonotonicNs();
    if(now<s->retryAt) return;
    if(OsAndroid_Render(window,s->generation,pixels,w,h,damage,scale,NULL)>0)
        s->expose=expose;
    else {
        s->expose=-1;
        s->retryAt=now+UINT64_C(1000000000);
    }
}

static int ShowMessage(void* user,const OpenBGI_Request_t* request,int positive,int negative)
{
    AndroidHost_t* host=user;
    int attached;
    JNIEnv* env=OsAndroid_GetEnv(&attached);
    if(!env) return 0;
    char text[8192],caption[1024];
    OsAndroid_ScriptToUtf8(request->u.message.text?request->u.message.text:"",text,sizeof text);
    OsAndroid_ScriptToUtf8(request->u.message.caption?request->u.message.caption:"",caption,sizeof caption);
    OsAndroid_Log(caption);
    OsAndroid_Log(text);
    if(!strcmp(caption,"Error!!")) OsAndroid_ReportError(text);
    jsize nt=(jsize)strlen(text),nc=(jsize)strlen(caption);
    jbyteArray body=(*env)->NewByteArray(env,nt);
    jbyteArray title=body?(*env)->NewByteArray(env,nc):NULL;
    int shown=0;
    if(title) {
        if(nt) (*env)->SetByteArrayRegion(env,body,0,nt,(const jbyte*)text);
        if(nc) (*env)->SetByteArrayRegion(env,title,0,nc,(const jbyte*)caption);
        pthread_mutex_lock(&host->lock);
        unsigned serial=++host->dialogSerial;
        host->dialogResult=negative?negative:positive;
        host->dialogPending=1;
        pthread_mutex_unlock(&host->lock);
        (*env)->CallVoidMethod(env,host->activity,host->messageBox,body,title,
            (jint)request->u.message.flags,(jint)serial);
        shown=!(*env)->ExceptionCheck(env);
        if(!shown) {
            pthread_mutex_lock(&host->lock);
            host->dialogPending=0;
            pthread_mutex_unlock(&host->lock);
        }
    }
    if((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if(body) (*env)->DeleteLocalRef(env,body);
    if(title) (*env)->DeleteLocalRef(env,title);
    OsAndroid_ReleaseEnv(attached);
    return shown;
}

static int MessageResult(void* user)
{
    AndroidHost_t* host=user;
    pthread_mutex_lock(&host->lock);
    int result=host->dialogPending?0:host->dialogResult;
    pthread_mutex_unlock(&host->lock);
    return result;
}

__attribute__((visibility("default"))) int OsAndroid_RunUpstream(AndroidHost_t* host)
{
    AndroidGameSettings_t settings;
    OsAndroid_ReadGameSettings(&settings);
    char args[96];
    const char* error=AndroidHostGameOptions(&settings,args,sizeof args);
    if(error) { OsAndroid_Log(error); OS_MessageBox(error,"OpenBGI 1.0",0); return 1; }
    OsAndroid_Log("Engine: OpenBGI upstream 1.0");
    OsAndroid_Log(args);

    OpenBGI_Host_t api=AndroidHostConfig();
    api.caps=OPENBGI_CAP_MESSAGES;
    Session s={.engine=OpenBGI_New(&api),.expose=-1};
    if(!s.engine) return 1;
    HostRequests_t requests={0};
    const HostMessageUi_t ui={ShowMessage,MessageResult,host};
    int run=OpenBGI_Start(s.engine,args),stopped=0,result=0;
    for(;;) {
        if(run!=OPENBGI_RUNNING && !stopped) {
            HostAudioUpdate(&s.audio,s.engine,0);
            result=OpenBGI_Stop(s.engine);
            stopped=1;
        }
        int waiting=HostRequestsPump(&requests,s.engine,&ui);
        if(stopped && !waiting) break;
        ANativeWindow* window=NextFrame(host,&s);
        if(!window) break;
        if(!stopped) {
            HostAudioUpdate(&s.audio,s.engine,s.focused || waiting);
            OpenBGI_PumpEvents(s.engine);
            OpenBGI_Frame_t frame={.size=sizeof frame};
            run=OpenBGI_RunFrame(s.engine,&frame);
            Present(&s,window,&frame,host->scaleMode);
        }
        ANativeWindow_release(window);
    }
    HostAudioUpdate(&s.audio,s.engine,0);
    if(!stopped) result=OpenBGI_Stop(s.engine);
    OpenBGI_Delete(s.engine);
    free(s.canvas.pixels);
    return result;
}
