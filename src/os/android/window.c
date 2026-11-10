/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * A logical RGBA canvas holds incremental engine blits. Presenting it
 * and mapping physical touch coordinates both use the same viewport.
 */
#include "android_internal.h"

#include <android/keycodes.h>

typedef struct Viewport
{
	int x, y, w, h;
} Viewport_t;

static OsEventHandlers_t gEvents;
static uint32_t* gCanvas;
static int gWidth, gHeight, gVisible, gDirty, gAlive, gActive;
static int gClose, gQuit, gDcToken;
static unsigned gRedraw;
static int gMouseX, gMouseY;
static uint8_t gKeys[256];
static int gGesture, gTwoFingers, gDragged;
static float gStartX, gStartY, gScrollY;

static Viewport_t Viewport(int w, int h)
{
	Viewport_t v = {0, 0, w, h};
	if((int64_t)w * gHeight > (int64_t)h * gWidth)
		v.w = (int)((int64_t)h * gWidth / gHeight);
	else
		v.h = (int)((int64_t)w * gHeight / gWidth);
	v.x = (w - v.w) / 2;
	v.y = (h - v.h) / 2;
	return v;
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
	gDirty = 1;
	return 1;
}

static void PresentLocked(ANativeWindow* window)
{
	ANativeWindow_Buffer b;
	Viewport_t v;
	int y;
	if(!window || !gCanvas || !gVisible || !gDirty || ANativeWindow_lock(window, &b, NULL) != 0)
		return;
	if(b.format == WINDOW_FORMAT_RGBA_8888 || b.format == WINDOW_FORMAT_RGBX_8888)
	{
		v = Viewport(b.width, b.height);
		for(y = 0; y < b.height; y++)
		{
			uint32_t* row = (uint32_t*)b.bits + (size_t)y * b.stride;
			int x;
			if(v.w <= 0 || v.h <= 0 || y < v.y || y >= v.y + v.h)
			{
				for(x = 0; x < b.width; x++)
					row[x] = 0xff000000u;
			}
			else
			{
				const uint32_t* src = gCanvas + (size_t)((int64_t)(y - v.y) * gHeight / v.h) * gWidth;
				for(x = 0; x < v.x; x++)
					row[x] = 0xff000000u;
				for(; x < v.x + v.w; x++)
					row[x] = src[(int64_t)(x - v.x) * gWidth / v.w] | 0xff000000u;
				for(; x < b.width; x++)
					row[x] = 0xff000000u;
			}
		}
		gDirty = 0;
	}
	ANativeWindow_unlockAndPost(window);
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
	gDirty = 1;
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

static int VirtualKey(int code)
{
	switch(code)
	{
		case AKEYCODE_BACK:
		case AKEYCODE_ESCAPE: return 0x1b;
		case AKEYCODE_ENTER:
		case AKEYCODE_DPAD_CENTER: return 0x0d;
		case AKEYCODE_DPAD_UP: return 0x26;
		case AKEYCODE_DPAD_DOWN: return 0x28;
		case AKEYCODE_DPAD_LEFT: return 0x25;
		case AKEYCODE_DPAD_RIGHT: return 0x27;
		case AKEYCODE_SPACE: return 0x20;
		case AKEYCODE_CTRL_LEFT:
		case AKEYCODE_CTRL_RIGHT: return 0x11;
		default: return 0;
	}
}

typedef struct InputEvent
{
	int type, action, key, repeat, width, height;
	float x, y, wheel;
} InputEvent_t;

/* Copy the event while the Activity owns the queue. Dispatch engine
 * callbacks after unlocking, since a callback may present a new frame. */
static int ReadInputLocked(AndroidHost_t* host, InputEvent_t* out)
{
	AInputEvent* event;
	AInputQueue* queue = host->input;
	int handled = 0;
	if(!queue || AInputQueue_getEvent(queue, &event) < 0)
		return 0;
	if(AInputQueue_preDispatchEvent(queue, event))
		return 1;
	out->type = AInputEvent_getType(event);
	if(out->type == AINPUT_EVENT_TYPE_KEY)
	{
		out->action = AKeyEvent_getAction(event);
		out->key = VirtualKey(AKeyEvent_getKeyCode(event));
		out->repeat = AKeyEvent_getRepeatCount(event) != 0;
		handled = out->key != 0;
	}
	else if(out->type == AINPUT_EVENT_TYPE_MOTION && host->window)
	{
		out->action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
		out->x = AMotionEvent_getX(event, 0);
		out->y = AMotionEvent_getY(event, 0);
		out->wheel = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
		out->width = ANativeWindow_getWidth(host->window);
		out->height = ANativeWindow_getHeight(host->window);
		handled = 1;
	}
	AInputQueue_finishEvent(queue, event, handled);
	return 1;
}

static void DispatchInput(const InputEvent_t* e)
{
	int inside;
	if(!gActive)
		return;
	if(e->type == AINPUT_EVENT_TYPE_KEY && e->key)
	{
		if(e->action == AKEY_EVENT_ACTION_DOWN)
		{
			gKeys[e->key] = 0x80;
			if(gEvents.key_down)
				gEvents.key_down(e->key, e->repeat);
		}
		else if(e->action == AKEY_EVENT_ACTION_UP)
		{
			gKeys[e->key] = 0;
			if(gEvents.key_up)
				gEvents.key_up(e->key);
		}
		return;
	}
	if(e->type != AINPUT_EVENT_TYPE_MOTION)
		return;
	inside = MapPoint(e->x, e->y, e->width, e->height);
	if(inside && gEvents.mouse_move)
		gEvents.mouse_move(gMouseX, gMouseY);
	switch(e->action)
	{
		case AMOTION_EVENT_ACTION_DOWN:
			gGesture = inside;
			gTwoFingers = gDragged = 0;
			gStartX = e->x;
			gScrollY = gStartY = e->y;
			break;
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			gTwoFingers = 1;
			break;
		case AMOTION_EVENT_ACTION_MOVE:
			if(gGesture && !gTwoFingers)
			{
				float step = e->height / 12.0f;
				if(step > 0 && (e->y - gScrollY >= step || gScrollY - e->y >= step))
				{
					int delta = (int)((e->y - gScrollY) / step);
					gDragged = 1;
					gScrollY += delta * step;
					if(gEvents.mouse_wheel)
						gEvents.mouse_wheel(delta * 120);
				}
			}
			break;
		case AMOTION_EVENT_ACTION_UP:
			if(gGesture && inside && !gDragged &&
				e->x - gStartX < e->width / 15.0f && gStartX - e->x < e->width / 15.0f &&
				e->y - gStartY < e->height / 15.0f && gStartY - e->y < e->height / 15.0f)
			{
				MouseButton(gTwoFingers, 1);
				MouseButton(gTwoFingers, 0);
			}
			gGesture = 0;
			break;
		case AMOTION_EVENT_ACTION_CANCEL:
			gGesture = 0;
			break;
		case AMOTION_EVENT_ACTION_SCROLL:
			if(gEvents.mouse_wheel && e->wheel)
				gEvents.mouse_wheel((int)(e->wheel * 120));
			break;
	}
}

int OS_PumpMessages(void)
{
	AndroidHost_t* host = gAndroidHost;
	InputEvent_t event = {0};
	int input, active, destroyed;
	pthread_mutex_lock(&host->lock);
	active = host->focused && host->resumed && host->window;
	destroyed = host->destroyed;
	if(gRedraw != host->redraw)
	{
		gRedraw = host->redraw;
		gDirty = 1;
	}
	if(host->resumed)
		PresentLocked(host->window);
	input = ReadInputLocked(host, &event);
	pthread_mutex_unlock(&host->lock);
	if(gQuit || destroyed)
	{
		gQuit = 0;
		if(gEvents.quit)
			gEvents.quit();
		return 1;
	}
	if(gActive != active)
	{
		int vk;
		gActive = active;
		if(!active)
		{
			for(vk = 1; vk < (int)sizeof gKeys; vk++)
				if(gKeys[vk] && gEvents.key_up)
					gEvents.key_up(vk);
			memset(gKeys, 0, sizeof gKeys);
			gGesture = 0;
		}
		if(gEvents.activate)
			gEvents.activate(active);
	}
	if(gClose)
	{
		gClose = 0;
		if(gEvents.close_request)
			gEvents.close_request();
		return 1;
	}
	if(input)
		DispatchInput(&event);
	if(!active && !input)
		OS_SleepMs(16);
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
	OS_SjisToUtf8(title ? title : "", utf8, sizeof utf8);
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
