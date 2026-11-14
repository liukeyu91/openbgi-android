/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * A logical RGBA canvas holds incremental engine blits. Presenting it
 * and mapping physical touch coordinates both use the same viewport.
 */
#include "android_internal.h"

#include "viewport.h"
#include "renderer.h"
#include "canvas.h"
#include "bgi/codec.h"

static OsEventHandlers_t gEvents;
static uint32_t* gCanvas;
static int gWidth, gHeight, gVisible, gDirty, gAlive, gActive;
static int gClose, gQuit, gDcToken;
static unsigned gRedraw;
static AndroidDamage_t gDamage;
static uint64_t gLastFrame,gRetryAt;
static int gMouseX, gMouseY;
static uint8_t gKeys[256];

static Viewport_t Viewport(int w,int h)
{
    return AndroidViewport(w,h,gWidth,gHeight,gAndroidHost->scaleMode);
}

static int SetCanvas(int w, int h)
{
	uint32_t* pixels;
	if(w == gWidth && h == gHeight && gCanvas)
		return 1;
	if(w <= 0 || h <= 0 || (int64_t)w * h > 8192 * 4096)
		return 0;
	pixels = calloc((size_t)w * h, sizeof *pixels);
	if(!pixels)
		return 0;
	free(gCanvas);
	gCanvas = pixels;
	gWidth = w;
	gHeight = h;
	OsAndroid_EditCommand(ANDROID_EDIT_CANVAS, w, h, 0, 0, NULL);
	gDirty = 1;
	gDamage = (AndroidDamage_t){0,0,w,h};
	return 1;
}

static uint32_t SurfacePixel(const OsSurface_t* src, int x, int y)
{
	const uint8_t* row = (const uint8_t*)src->pixels + (size_t)y * src->pitch;
	uint32_t r, g, b;
	if(src->bpp == 16)
	{
		uint16_t p;
		memcpy(&p, row + (size_t)x * 2, sizeof p);
		r = (p >> 10) & 31;
		g = (p >> 5) & 31;
		b = p & 31;
		r = (r << 3) | (r >> 2);
		g = (g << 3) | (g >> 2);
		b = (b << 3) | (b >> 2);
	}
	else
	{
		const uint8_t* p = row + (size_t)x * 4;
		b = p[0];
		g = p[1];
		r = p[2];
	}
	return r | (g << 8) | (b << 16) | 0xff000000u;
}

static void Blit(int dx, int dy, int dw, int dh, const OsSurface_t* src,
	int sx, int sy, int sw, int sh, const int* clip)
{
	int l = dx, t = dy, r, b, y;
	if(!gCanvas || !src || !src->pixels || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 ||
		(src->bpp != 16 && src->bpp != 32))
		return;
	r = (int64_t)dx + dw < gWidth ? (int)((int64_t)dx + dw) : gWidth;
	b = (int64_t)dy + dh < gHeight ? (int)((int64_t)dy + dh) : gHeight;
	if(clip)
	{
		if(l < clip[0]) l = clip[0];
		if(t < clip[1]) t = clip[1];
		if(r > clip[2]) r = clip[2];
		if(b > clip[3]) b = clip[3];
	}
	if(l < 0) l = 0;
	if(t < 0) t = 0;
    if(dw==sw && dh==sh && l<r && t<b)
    {
        /* Ordinary presents are already logical-size pixels. Clip the source
         * columns once and copy rows without a per-pixel scale division. */
        int64_t pos=(int64_t)sx+l-dx,first=pos<0?0:pos,last=pos+(r-l);
        if(last>src->width)last=src->width;
        if(first<last) {
            int target=l+(int)(first-pos),count=(int)(last-first);
            for(y=t;y<b;y++) {
                int64_t fromY=(int64_t)sy+y-dy;
                if(fromY>=0 && fromY<src->height)
                    AndroidCanvasCopyRow(gCanvas+(size_t)y*gWidth+target,
                        (const uint8_t*)src->pixels+(size_t)fromY*src->pitch+(size_t)first*(src->bpp/8),count,src->bpp);
            }
        }
        AndroidDamageAdd(&gDamage,l,t,r,b,gWidth,gHeight);gDirty=1;return;
    }
	for(y = t; y < b; y++)
	{
		int64_t fromY = sy + ((int64_t)y - dy) * sh / dh;
		int x;
		if(fromY < 0 || fromY >= src->height)
			continue;
		for(x = l; x < r; x++)
		{
			int64_t fromX = sx + ((int64_t)x - dx) * sw / dw;
			if(fromX >= 0 && fromX < src->width)
				gCanvas[(size_t)y * gWidth + x] = SurfacePixel(src, (int)fromX, (int)fromY);
		}
	}
	AndroidDamageAdd(&gDamage,l,t,r,b,gWidth,gHeight);
	if(l<r && t<b)gDirty = 1;
}

static int MapPoint(float x, float y, int width, int height)
{
	Viewport_t v;
	if(!gCanvas || width <= 0 || height <= 0)
		return 0;
	v = Viewport(width, height);
	if(v.w <= 0 || v.h <= 0)
		return 0;
	gMouseX = (int)((x - v.x) * gWidth / v.w);
	gMouseY = (int)((y - v.y) * gHeight / v.h);
	return x >= v.x && x < v.x + v.w && y >= v.y && y < v.y + v.h;
}

static void MouseButton(int button, int down)
{
	gKeys[button + 1] = down ? 0x80 : 0;
	if(gEvents.mouse_button)
		gEvents.mouse_button(button, down, gMouseX, gMouseY);
}

typedef AndroidInputEvent_t InputEvent_t;

static int ReadInputLocked(AndroidHost_t* host, InputEvent_t* out)
{
    if(!host->inputCount) return 0;
    *out = host->input[0];
    memmove(host->input, host->input + 1, --host->inputCount * sizeof *out);
    return 1;
}

static void DispatchInput(const InputEvent_t* e)
{
    int inside;
    if(!gActive)return;
    if(e->type==AINPUT_EVENT_TYPE_KEY){
        if(e->key<=0 || e->key>=256)return;
        if(e->action==AKEY_EVENT_ACTION_DOWN){gKeys[e->key]=0x80;if(gEvents.key_down)gEvents.key_down(e->key,e->repeat);}
        else if(e->action==AKEY_EVENT_ACTION_UP){gKeys[e->key]=0;if(gEvents.key_up)gEvents.key_up(e->key);}
        return;
    }
    if(e->type!=AINPUT_EVENT_TYPE_MOTION)return;
    if(e->action==ANDROID_POINTER_CANCEL){
        if(gKeys[1])MouseButton(0,0);if(gKeys[2])MouseButton(1,0);return;
    }
    inside=MapPoint(e->x,e->y,e->width,e->height);
    if((inside || gKeys[1] || gKeys[2]) && gEvents.mouse_move)gEvents.mouse_move(gMouseX,gMouseY);
    if(e->action==ANDROID_POINTER_DOWN && inside && e->key>=0 && e->key<=1 && !gKeys[e->key+1])MouseButton(e->key,1);
    else if(e->action==ANDROID_POINTER_UP && e->key>=0 && e->key<=1 && gKeys[e->key+1])MouseButton(e->key,0);
    else if(e->action==ANDROID_POINTER_WHEEL && inside && e->wheel && gEvents.mouse_wheel)gEvents.mouse_wheel((int)(e->wheel*120));
}

static void ResetInput(void)
{
    int vk;
    for(vk = 1; vk < (int)sizeof gKeys; ++vk)
        if(gKeys[vk])
        {
            if(vk <= 2) MouseButton(vk - 1, 0);
            else { gKeys[vk]=0; if(gEvents.key_up)gEvents.key_up(vk); }
        }
    memset(gKeys, 0, sizeof gKeys);
}

int OS_PumpMessages(void)
{
    AndroidHost_t* host = gAndroidHost;
    InputEvent_t event = {0};
    ANativeWindow* window;
    unsigned redraw;
    uint64_t frame;
    int input,active,destroyed,reset;
    pthread_mutex_lock(&host->lock);
    if(!host->resumed && !host->destroyed){
        pthread_mutex_unlock(&host->lock);
        BGI_CbgCacheClear();
        ResetInput();
        if(gActive && gEvents.activate)gEvents.activate(0);
        gActive=0;
        OsAndroid_RenderClose();gDirty=1;
        pthread_mutex_lock(&host->lock);
        while(!host->resumed && !host->destroyed)pthread_cond_wait(&host->changed,&host->lock);
    }
    active=host->focused && host->resumed && host->window;
    destroyed=host->destroyed;reset=host->inputReset;host->inputReset=0;
    redraw=host->redraw;frame=host->frameSerial;
    window=!destroyed && host->resumed?host->window:NULL;
    if(window)ANativeWindow_acquire(window);
    input=ReadInputLocked(host,&event);
    pthread_mutex_unlock(&host->lock);
    /* Never hold the UI/lifecycle mutex across driver calls or eglSwapBuffers. */
    if(gRedraw!=redraw){gRedraw=redraw;gDirty=1;gRetryAt=0;}
    if(!window){OsAndroid_RenderClose();gDirty=1;}
    else if(gDirty && gVisible && gCanvas && frame!=gLastFrame && OsAndroid_ClockNs()>=gRetryAt){
        int result=OsAndroid_Render(window,redraw,gCanvas,gWidth,gHeight,gDamage,host->scaleMode);
        gLastFrame=frame;
        if(result>0){gDirty=0;gDamage=(AndroidDamage_t){0};gRetryAt=0;}
        else if(result<0)gRetryAt=OsAndroid_ClockNs()+1000000000u;
    }
    if(window)ANativeWindow_release(window);
    if(gQuit || destroyed){gQuit=0;if(gEvents.quit)gEvents.quit();return 1;}
    if(gActive!=active || reset){
        gActive=active;if(!active || reset)ResetInput();
        if(gEvents.activate)gEvents.activate(active);
    }
    if(gClose){gClose=0;if(gEvents.close_request)gEvents.close_request();return 1;}
    if(input)DispatchInput(&event);
    if(!active && !input)OS_SleepMs(16);
    return input;
}

void OS_SetEventHandlers(const OsEventHandlers_t* h)
{
	gEvents = h ? *h : (OsEventHandlers_t){0};
}

int OS_WindowCreate(const OsWindowDesc_t* d)
{
	if(!SetCanvas(d->width, d->height))
		return 0;
	gAlive = 1;
	gQuit = gClose = gVisible = gActive = 0;
	gRedraw = 0;
	gLastFrame = gRetryAt = 0;
	memset(gKeys, 0, sizeof gKeys);
	OS_WindowSetTitle(d->title);
	if(gEvents.created)
		gEvents.created();
	return 1;
}

void OS_WindowDestroy(void)
{
	if(!gAlive)
		return;
	gAlive = gVisible = 0;
	OsAndroid_RenderClose();
	{AndroidRenderStats_t st=OsAndroid_RenderStats();char log[240];
	 snprintf(log,sizeof log,"EGL totals: frames=%llu full=%llu partial=%llu bytes=%llu errors=%llu",
	 (unsigned long long)st.frames,(unsigned long long)st.full,(unsigned long long)st.partial,
	 (unsigned long long)st.bytes,(unsigned long long)st.errors);OsAndroid_Log(log);}
	free(gCanvas);
	gCanvas = NULL;
	gWidth = gHeight = 0;
	if(gEvents.destroyed)
		gEvents.destroyed();
}

int OS_WindowAlive(void) { return gAlive; }
int OS_WindowActive(void) { return gActive; }
int OS_WindowMinimized(void) { return !gActive; }
void OS_WindowMinimize(void) {}
void OS_WindowClose(void) { gClose = 1; }
void OS_PostQuit(void) { gQuit = 1; }
void OS_WindowMove(int x, int y) { BGI_UNUSED(x); BGI_UNUSED(y); }
void OS_ImeStatus(int open) { BGI_UNUSED(open); }
void OS_DragAccept(int accept) { BGI_UNUSED(accept); }

void OS_WindowShow(int show)
{
	gVisible = show != 0;
	gDirty = 1;
	if(show && gEvents.paint)
		gEvents.paint();
}

void OS_WindowSetTitle(const char* title)
{
	char utf8[512];
	OsAndroid_ScriptToUtf8(title ? title : "", utf8, sizeof utf8);
	if(utf8[0])
		OsAndroid_Log(utf8);
}

void OS_ScreenSize(int* w, int* h)
{
	AndroidHost_t* host = gAndroidHost;
	pthread_mutex_lock(&host->lock);
	*w = host->window ? ANativeWindow_getWidth(host->window) : 1280;
	*h = host->window ? ANativeWindow_getHeight(host->window) : 720;
	pthread_mutex_unlock(&host->lock);
}

void OS_WindowGetRect(int* x, int* y, int* w, int* h)
{
	*x = *y = 0;
	*w = gWidth;
	*h = gHeight;
}

void OS_FrameMetrics(int* fw, int* fh) { *fw = *fh = 0; }
int OS_FrameTop(void) { return 0; }

int OS_DisplaySetMode(int fullscreen, int w, int h, int bpp)
{
	BGI_UNUSED(fullscreen);
	BGI_UNUSED(bpp);
	return w > 0 && h > 0;
}

void OS_DisplayRestore(void) {}
void OS_WindowSetFullscreen(int w, int h) { SetCanvas(w, h); }
void OS_WindowSetWindowed(int x, int y, int w, int h) { BGI_UNUSED(x); BGI_UNUSED(y); SetCanvas(w, h); }
void OS_WindowSetPicture(int w, int h) { SetCanvas(w, h); }

OsDc_t* OS_WindowGetDc(void) { return gCanvas ? &gDcToken : NULL; }
void OS_WindowReleaseDc(OsDc_t* dc) { BGI_UNUSED(dc); }

void OS_BlitToWindow(OsDc_t* dc, int dx, int dy, int w, int h, const OsSurface_t* src, int sx, int sy)
{
	BGI_UNUSED(dc);
	Blit(dx, dy, w, h, src, sx, sy, w, h, NULL);
}

void OS_StretchToWindow(OsDc_t* dc, int x, int y, int w, int h, const OsSurface_t* src, const int* clip)
{
	BGI_UNUSED(dc);
	if(src)
		Blit(x, y, w, h, src, 0, 0, src->width, src->height, clip);
}

void OS_WindowFillBlack(void)
{
	if(gCanvas)
		memset(gCanvas, 0, (size_t)gWidth * gHeight * sizeof *gCanvas);
	gDirty = 1;
	gDamage=(AndroidDamage_t){0,0,gWidth,gHeight};
}

void OS_CursorSetShape(int shape) { BGI_UNUSED(shape); }
void OS_CursorShow(int show) { BGI_UNUSED(show); }
void OS_CursorGetPos(int* x, int* y) { *x = gMouseX; *y = gMouseY; }
void OS_CursorSetPos(int x, int y) { gMouseX = x; gMouseY = y; }
void OS_ScreenToClient(int* x, int* y) { BGI_UNUSED(x); BGI_UNUSED(y); }
void OS_ClientToScreen(int* x, int* y) { BGI_UNUSED(x); BGI_UNUSED(y); }
int OS_MouseButtonsSwapped(void) { return 0; }
int OS_KeyState(int vk) { return vk >= 0 && vk < (int)sizeof gKeys && gKeys[vk] != 0; }
void OS_KeyboardState(uint8_t out[256]) { memcpy(out, gKeys, sizeof gKeys); }
