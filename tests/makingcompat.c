/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "bgi/vm.h"
#include "bgi/version.h"
#include "bgi/display.h"
#include "bgi/sys.h"
#include "bgi/error.h"
#include "bgi/os.h"
#include "bgi/gfx.h"
#include "bgi/gfx/gfxmgr.h"
#include "bgi/gfx/compositor.h"
#include "bgi/gfx/screens.h"
#include "bgi/gfx/window.h"
#include "bgi/gfx/text.h"
#include "bgi/gfx/background.h"
#include "bgi/snd/wavemaster.h"
#include <stdio.h>
static int failures, checks;
#define CHECK(c)                                   \
	do                                             \
	{                                              \
		checks++;                                  \
		if(!(c))                                   \
		{                                          \
			printf("FAIL %d: %s\n", __LINE__, #c); \
			failures++;                            \
		}                                          \
	} while(0)
static int seAccepted(uint32_t slot, Thread_t* t)
{
	BgiTryFrame_t f;
	if(BGI_TRY(f))
	{
		CheckSeSlot(slot, t);
		BGI_END_TRY(f);
		return 1;
	}
	return 0;
}
int main(void)
{
	setenv("BGI_MSGBOX_STDERR", "1", 1);
	OS_Init();
	Engine_ResetGames();
	CHECK(Engine_SelectProfile("1.659"));
	Display_InitSizeTable();
	CHECK(Display_ModeCount() == 8);
	CHECK(Wm_SeSlotCount() == 64);
	CHECK(Display_SetSizeEntry(8, 1920, 1080) == 1);
	CHECK(Engine_SelectProfile("1.659.8"));
	Vm_TablesInit();
	Display_InitSizeTable();
	CHECK(Display_ModeCount() == 16);
	CHECK(Wm_SeSlotCount() == 128);
	const int w[16] = {320, 640, 800, 1024, 1024, 1280, 1280, 1920, 1920, 2560, 2560, 3840, 3840, 1600, 1366, 960};
	const int h[16] = {240, 480, 600, 768, 576, 960, 720, 1440, 1080, 1920, 1440, 2880, 2160, 900, 768, 540};
	for(int i = 0; i < 16; i++)
		CHECK(gScreenWidths[i] == w[i] && gScreenHeights[i] == h[i]);
	CHECK(Display_SetSizeEntry(15, 960, 540) == 0);
	CHECK(Display_SetSizeEntry(16, 1, 1) == 1);
	CHECK(Display_SetSizeEntry(-1, 1, 1) == 1);
	CHECK(Display_SetSizeEntry(8, 0, 1) == 2);
	Thread_t* t = Thread_New(64, 256, 256);
	CHECK(seAccepted(78, t));
	CHECK(seAccepted(127, t));
	CHECK(!seAccepted(128, t));
	CHECK(!seAccepted(UINT32_MAX, t));
	Thread_Push(t, 0x12345678);
	Thread_Push(t, 0);
	CHECK(vm_optable_80[0x21](t) == 0);
	CHECK(Thread_Pop(t) == 1);
	CHECK(Thread_Pop(t) == 0x12345678);
	gTextLastPos[0] = 19;
	gTextLastPos[1] = 23;
	Thread_Push(t, 0x12345678);
	Thread_Push(t, 0x08000010);
	Thread_Push(t, 0x100);
	CHECK(vm_optable_92[0x9b](t) == 0);
	CHECK(*(int32_t*)(t->data + 16) == 19 && *(int32_t*)(t->data + 20) == 23);
	CHECK(Thread_Pop(t) == 0x12345678);
	Background_t* bg = Background_Create(12);
	CHECK(bg->obj.vt->setParam(&bg->obj, 0x8e, 3, 0) == 0);
	CHECK(((Bg12_t*)bg)->layer[0].centreCurve == 3);
	bg->obj.vt->destroy(&bg->obj, 1);
	RichChar_t n[4] = {{0}};
	RichState_t rich = {{0}, n};
	for(int i = 0; i < 3; i++)
		n[i].next = &n[i + 1];
	n[0].y = 0;
	n[0].emHeight = 40;
	n[1].y = 0;
	n[1].emHeight = 48;
	n[2].y = 50;
	n[2].emHeight = 20;
	n[3].y = 100;
	n[3].emHeight = 60;
	TextMetrics_Capture(&rich, 3, 40, 0);
	uint32_t id = TextMetrics_Save();
	CHECK(id != 0);
	CHECK(TextMetrics_Take(NULL, id) == 3);
	CHECK(TextMetrics_Take(NULL, id) == 3);
	int32_t out[5] = {-1, -1, -1, 11, 12};
	CHECK(TextMetrics_Take(out, id) == 3);
	CHECK(out[0] == 48 && out[1] == 20 && out[2] == 60 && out[3] == 11 && out[4] == 12);
	CHECK(TextMetrics_Take(NULL, id) == 0);
	TextMetrics_Capture(&rich, 3, 40, 0);
	id = TextMetrics_Save();
	Thread_Push(t, 0);
	Thread_Push(t, id);
	CHECK(vm_optable_92[0x94](t) == 0);
	CHECK(Thread_Pop(t) == 3);
	TextMetrics_Clear();
	CHECK(TextMetrics_Take(NULL, id) == 0);
	Bmp_t source, dest, map;
	Bmp_Alloc(&source, 2, 2, PM_RGB32);
	Bmp_Alloc(&dest, 2, 2, PM_RGB32);
	uint16_t mapPixels[12] = {0};
	map = (Bmp_t){(uint8_t*)mapPixels,12,2,2,PM_VECDIST,6};
	((uint32_t*)source.pixels)[0] = 16;
	((uint32_t*)source.pixels)[1] = 80;
	((uint32_t*)(source.pixels + source.pitch))[0] = 144;
	((uint32_t*)(source.pixels + source.pitch))[1] = 208;
	for(int y = 0; y < 2; y++)
		for(int x = 0; x < 2; x++)
		{
			uint16_t* v = (uint16_t*)(map.pixels + y * map.pitch + x * 6);
			v[0] = v[1] = 16384;
			v[2] = 0;
		}
	uint32_t table = 0x00020002;
	CHECK(Blit_RippleEx(&dest, &source, &source, &map, &table, 1) == 0);
	CHECK(((uint32_t*)dest.pixels)[0] == 112);
	CHECK(((uint32_t*)(dest.pixels + dest.pitch))[1] == 52);
	Bmp_Free(&source);
	Bmp_Free(&dest);
	bg = Background_Create(8);
	CHECK(bg->obj.vt->setParam(&bg->obj, 0xff, 1, 0) == 0);
	CHECK(((Bg8_t*)bg)->smooth == 1);
	bg->obj.vt->destroy(&bg->obj, 1);

	Gfx_Create(0x1000, 1);
	Gfx_SetupScreen(gGfx, 64, 32, PM_RGB32, 0, 0, 0, 0);
	uint32_t sprite = Gfx_SpriteCreate(gGfx);
	int32_t bitmap = 0;
	CHECK(Gfx_ObjGetParam(gGfx, sprite, 0x10, &bitmap) == 0 && bitmap == -1);
	uint32_t window;
	CHECK(Gfx_WindowCreate(gGfx, &window, 64, 32) == 0);
	Thread_Push(t, window);
	CHECK(vm_optable_92[0x95](t) == 0);
	CHECK(Thread_Pop(t) == (uint32_t)Window_GetFontSize((Window_t*)Gfx_FindWindow(gGfx, window)));
	uint32_t handle;
	CHECK(Gfx_ParticleCreate(gGfx, &handle, 64, 32) == 0);
	ParticleScreen_t* p = (ParticleScreen_t*)Gfx_FindParticle(gGfx, handle);
	int32_t depths[2] = {100, 200}, priorities[2] = {0, 10};
	int before = gGfx->comp->objectCount;
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 0, depths, priorities) == 7);
	CHECK(p->layerCount == 0);
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 17, depths, priorities) == 7);
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 2, depths, priorities) == 0);
	CHECK(p->layerCount == 2 && gGfx->comp->objectCount == before + 2);
	Gfx_ParticleShow(gGfx, handle, 1);
	Gfx_ParticleSetDraw(gGfx, handle, 3, 4, 0x80, 256, 30);
	ParticleScreen_Update(p);
	ParticleScreen_Redraw(p);
	Gfx_RenderAll(gGfx);
	priorities[0] = 65536;
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 2, depths, priorities) == 8);
	CHECK(p->layerCount == 2);
	priorities[0] = 0;
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 1, depths, priorities) == 0);
	CHECK(gGfx->comp->objectCount == before + 1);
	CHECK(Gfx_ParticleDelete(gGfx, handle));
	CHECK(gGfx->comp->objectCount == before - 1);
	CHECK(Gfx_ParticleCreate(gGfx, &handle, 64, 32) == 0);
	p = (ParticleScreen_t*)Gfx_FindParticle(gGfx, handle);
	CHECK(ParticleScreen_SetLayers(p, gGfx->comp, 2, depths, priorities) == 0);
	Gfx_Destroy();
	Thread_Delete(t);
	TextMetrics_Clear();
	OS_Shutdown();
	printf("Making compatibility: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
