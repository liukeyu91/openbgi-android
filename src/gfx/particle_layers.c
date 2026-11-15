/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "bgi/gfx/screens.h"
#include "bgi/gfx/compositor.h"
/* C0 06 assigns cumulative camera-depth spans to independently sorted
 * draw layers. They share the parent's simulation, visibility and blend. */
typedef struct ParticleLayer
{
	DispObj_t obj;
	ParticleScreen_t* parent;
	Compositor_t* comp;
	Bmp_t surface;
	int32_t nearZ, farZ, priority;
	uint32_t key;
	int visible, level;
} ParticleLayer;
static DispObjVtbl_t layerVtbl;
static int layerVisible(DispObj_t* o)
{
	ParticleLayer* l = (ParticleLayer*)o;
	return l->parent->obj.vt->isVisible(&l->parent->obj);
}
static uint32_t layerKey(DispObj_t* o)
{
	ParticleLayer* l = (ParticleLayer*)o;
	DispObj_t copy = l->parent->obj;
	copy.priority += l->priority;
	return copy.vt->sortKey(&copy);
}
static void layerLocal(DispObj_t* o, Rect_t* r)
{
	ParticleLayer* l = (ParticleLayer*)o;
	l->parent->obj.vt->localRect(&l->parent->obj, r);
}
static void layerScreen(DispObj_t* o, Rect_t* r)
{
	ParticleLayer* l = (ParticleLayer*)o;
	l->parent->obj.vt->screenRect(&l->parent->obj, r);
}
static void layerPosition(DispObj_t* o, int32_t* xy)
{
	ParticleLayer* l = (ParticleLayer*)o;
	l->parent->obj.vt->getDrawPos(&l->parent->obj, xy);
}
static void layerDraw(DispObj_t* o, Bmp_t* dst, const Rect_t* local, uint32_t minKey)
{
	ParticleLayer* l = (ParticleLayer*)o;
	Bmp_t image = l->surface;
	(void)minKey;
	if(!image.pixels)
		return;
	Bmp_Crop(&image, local);
	Bmp_BlitEffect(dst, &image, DispObj_GetEffect(&l->parent->obj), DispObj_EffectiveLevel(&l->parent->obj));
}
void ParticleScreen_ClearLayers(ParticleScreen_t* p)
{
	for(int i = 0; i < p->layerCount; i++)
	{
		ParticleLayer* l = &p->layers[i];
		Compositor_Remove(l->comp, &l->obj);
		Bmp_Free(&l->surface);
		DispObj_Dtor(&l->obj);
	}
	if(p->layerCount)
		Compositor_InvalidateAll(p->layers[0].comp);
	BGI_Free(p->layers);
	p->layers = NULL;
	p->layerCount = 0;
}
int ParticleScreen_SetLayers(ParticleScreen_t* p, Compositor_t* c, int count, const int32_t* depths, const int32_t* priorities)
{
	if(count < 1 || count > 16)
		return 7;
	for(int i = 0; i < count; i++)
		if(((uint32_t)p->obj.priority + (uint32_t)priorities[i]) >= 65536u)
			return 8;
	ParticleScreen_ClearLayers(p);
	p->layers = BGI_Calloc((size_t)count * sizeof *p->layers);
	p->layerCount = count;
	layerVtbl = DispObj_Vtbl;
	layerVtbl.isVisible = layerVisible;
	layerVtbl.sortKey = layerKey;
	layerVtbl.localRect = layerLocal;
	layerVtbl.screenRect = layerScreen;
	layerVtbl.getDrawPos = layerPosition;
	layerVtbl.draw = layerDraw;
	int32_t cumulative = 0;
	for(int i = 0; i < count; i++)
	{
		ParticleLayer* l = &p->layers[i];
		DispObj_Ctor(&l->obj, 4, p->obj.slotId);
		l->obj.vt = &layerVtbl;
		l->parent = p;
		l->comp = c;
		l->priority = priorities[count - 1 - i];
		l->nearZ = cumulative;
		cumulative = (int32_t)((uint32_t)cumulative + ((uint32_t)depths[i] << 8));
		l->farZ = cumulative;
		Bmp_AllocScreen(&l->surface, p->surf.w, p->surf.h, 1);
		Bmp_Clear(&l->surface, NULL);
		l->key = layerKey(&l->obj);
		l->visible = layerVisible(&l->obj);
		l->level = p->obj.level;
		Compositor_Add(c, &l->obj);
	}
	Compositor_InvalidateAll(c);
	return 0;
}
void ParticleScreen_UpdateLayers(ParticleScreen_t* p)
{
	for(int i = 0; i < p->layerCount; i++)
	{
		ParticleLayer* l = &p->layers[i];
		uint32_t key = layerKey(&l->obj);
		int visible = layerVisible(&l->obj);
		if(key != l->key)
		{
			Compositor_Resort(l->comp, &l->obj);
			l->key = key;
			Compositor_InvalidateAll(l->comp);
		}
		if(visible != l->visible || l->level != p->obj.level)
		{
			l->visible = visible;
			l->level = p->obj.level;
			Compositor_InvalidateAll(l->comp);
		}
	}
}
void ParticleScreen_RedrawLayers(ParticleScreen_t* p)
{
	static Rect_t rects[32768];
	int32_t count;
	for(int i = 0; i < p->layerCount; i++)
	{
		ParticleLayer* l = &p->layers[i];
		if(l->surface.w != p->surf.w || l->surface.h != p->surf.h)
		{
			Bmp_Free(&l->surface);
			Bmp_AllocScreen(&l->surface, p->surf.w, p->surf.h, 1);
		}
		Bmp_Clear(&l->surface, NULL);
		ParticleEngine_RenderRange(p->engine, &l->surface, p->cx, p->cy, &count, rects, l->nearZ, l->farZ);
	}
	if(p->layerCount)
		Compositor_InvalidateAll(p->layers[0].comp);
}
