/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Exercise "91 BB" through its VM handler and the real window compositor.
 */
#include "bgi/gfx.h"
#include "bgi/gfx/bmpmgr.h"
#include "bgi/gfx/sprite.h"
#include "bgi/gfx/window.h"
#include "bgi/os.h"
#include "bgi/input.h"
#include "bgi/panel.h"
#include "bgi/version.h"
#include "bgi/vm.h"

#include <stdio.h>
#include <string.h>

static int gFails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); gFails++; } } while(0)

static uint32_t Change(Thread_t* t, uint32_t id, int group, int button, int bitmap)
{
	uint32_t result;
	Thread_Push(t, 0x12345678);
	Thread_Push(t, id);
	Thread_Push(t, (uint32_t)group);
	Thread_Push(t, (uint32_t)button);
	Thread_Push(t, (uint32_t)bitmap);
	CHECK(vm_optable_91[0xbb](t) == 0);
	result = Thread_Pop(t);
	CHECK(Thread_Pop(t) == 0x12345678);
	return result;
}

static uint32_t Pixel(const Bmp_t* bmp, int x, int y)
{
	uint32_t colour;
	memcpy(&colour, bmp->pixels + y * bmp->pitch + x * 4, 4);
	return colour & 0xffffff;
}

int main(void)
{
	uint32_t window, otherWindow, id, otherId;
	Thread_t* t;
	Window_t* w;
	ButtonPanel_t* p;
	SpriteDesc_t desc = {0};
	SpriteGroup_t groups[2] = {{0}};
	SpriteButton_t buttons[2] = {{0}};
	Bmp_t bmp;
	int i, x, y;
	int32_t normal;
	OS_Init();
	CHECK(Engine_SelectProfile("1.494"));
	Vm_TablesInit();
	CHECK(vm_optable_91[0xbb] == NULL);
	CHECK(Engine_SelectProfile("1.529"));
	Vm_TablesInit();
	CHECK(vm_optable_91[0xbb] != NULL && vm_optable_91[0xbb] != Opcode_NotImplemented);
	CHECK(Engine_SelectProfile("1.636"));
	Vm_TablesInit();
	Gfx_Create(0x1000, 1);
	Gfx_SetupScreen(gGfx, 128, 64, PM_RGB32, 0, 0, 0, 0);
	t = Thread_New(64, 256, 256);
	for(i = 0; i < 2; i++)
	{
		CHECK(BmpMgr_Create(gBmpMgr, i, 8, 8, PM_RGB32) == 1);
		CHECK(BmpMgr_GetInfo(gBmpMgr, &bmp, i));
		for(y = 0; y < bmp.h; y++)
			for(x = 0; x < bmp.w; x++)
			{
				uint32_t colour = i ? 0x0055aa22 : 0x00aa2255;
				memcpy(bmp.pixels + y * bmp.pitch + x * 4, &colour, 4);
			}
	}
	CHECK(Gfx_WindowCreate(gGfx, &window, 128, 64) == 0);
	CHECK(Gfx_WindowCreate(gGfx, &otherWindow, 128, 64) == 0);
	w = (Window_t*)Gfx_FindWindow(gGfx, window);
	id = SubObj_Create(window, 1);
	otherId = SubObj_Create(otherWindow, 0);
	p = (ButtonPanel_t*)SubObj_Find(id);
	CHECK(Change(t, 0xffffffffu, 0, 0, 1) == 1);
	CHECK(Change(t, otherId, 0, 0, 1) == 4);
	CHECK(Change(t, id, 0, 0, 1) == 2); // not started
	for(i = 0; i < 2; i++)
	{
		buttons[i].enabled = i == 0;
		buttons[i].shown = i == 0;
		buttons[i].x = buttons[i].y = 8;
		buttons[i].bmpNormal = 0;
		buttons[i].bmpFocus = buttons[i].bmpSelected = buttons[i].bmpSelFocus = -1;
		buttons[i].hitMaskBmp = -1;
		groups[i].count = groups[i].columns = 1;
		groups[i].buttons = &buttons[i];
		groups[i].selected = -1;
	}
	desc.groupCount = 2;
	desc.groups = groups;
	desc.currentGroup = -1;
	CHECK(SpritePanel_Start(p, &desc) == 0);
	CHECK(w->subCount == 2 && w->sub[0] && !w->sub[1]);
	CHECK(Pixel(&w->comp, 10, 10) == 0xaa2255);
	CHECK(Change(t, id, -1, 0, 1) == 2);
	CHECK(Change(t, id, 2, 0, 1) == 2);
	CHECK(Change(t, id, 0, -1, 1) == 3);
	CHECK(Change(t, id, 0, 1, 1) == 3);
	p->p.dirty = 0;
	CHECK(Change(t, id, 0, 0, 1) == 0);
	CHECK(((Sprite_t*)w->sub[0])->bmp1 == 1);
	CHECK(Pixel(&w->comp, 10, 10) == 0x55aa22);
	CHECK(p->p.dirty);
	CHECK(PANEL_VT(p)->getBitmap(p, &normal, 0, 0, 0, 0) && normal == 0);
	CHECK(Change(t, id, 0, 0, 999) == 0); // original ignores a rejected bitmap
	CHECK(((Sprite_t*)w->sub[0])->bmp1 == 1);
	CHECK(Pixel(&w->comp, 10, 10) == 0x55aa22);
	p->p.dirty = 0;
	CHECK(Change(t, id, 1, 0, 1) == 0); // undrawn button remains absent
	CHECK(!w->sub[1] && !p->p.dirty);
	CHECK(SubObj_Delete(id));
	CHECK(Change(t, id, 0, 0, 1) == 1);
	{
		PanelButton_t choices[2] = {{0}};
		PanelGroup_t group = {0};
		PanelDesc_t choiceDesc = {0};
		int mouse[2];
		ButtonPanel_t* choicePanel = (ButtonPanel_t*)SubObj_Find(otherId);
		for(i = 0; i < 2; i++)
		{
			choices[i].enabled = 1;
			choices[i].y = i * 16;
			choices[i].bmpNormal = 0;
			choices[i].bmpSelected = 1;
			choices[i].bmpFocus = choices[i].hitMaskBmp = -1;
		}
		group.count = 2;group.columns = 1;group.buttons = choices;
		group.selected = -1;group.selectable = group.hoverSelects = 1;group.linkId = -1;
		choiceDesc.groupCount = 1;choiceDesc.groups = &group;choiceDesc.currentGroup = 0;
		choiceDesc.modal = 1;choiceDesc.inputMask = 0x2c2;choiceDesc.keyMap = 1;choiceDesc.selectOnDecide = 1;
		Input_ResetLayers();
		GetMouseClientPos(mouse);
		CHECK(mouse[0] == 0 && mouse[1] == 0); // no OS window in this test
		CHECK(ButtonPanel_Start(choicePanel, &choiceDesc) == 0);
		CHECK(choicePanel->groups[0].selected == -1);
		choicePanel->focus = -1; // pointer arrival and click in the same update
		choicePanel->input = 1;
		CHECK(ButtonPanel_Update(choicePanel) == 1);
		CHECK(choicePanel->decidedGroup == 0 && choicePanel->decidedIndex == 0 && choicePanel->decidedFlag == 1);
	}
	Panel_DeleteAll();
	Thread_Delete(t);
	Gfx_Destroy();
	OS_Shutdown();
	printf("%s\n", gFails ? "failed" : "ok");
	return gFails != 0;
}
