/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * blit_ripple.c - the ripple blitters (effector mode 2, background type 8, sprite
 *                 mode 4) of inc/bgi/gfx/bitmap.h: a source read through a
 *                 ripple's vector map
 *
 * The map is a PM_VECDIST bitmap of the source view's size (VecDist_Radial
 * / VecDist_Linear); the table is the per-ring displacement that
 * BmpMgr_RippleFill produces from a ripple definition, one 32-bit entry
 * {int16 ax, int16 ay} per ring.
 */
#include "blit_internal.h"

/*
 * A PM_VECDIST pixel is 6 bytes: int16 vx, int16 vy, uint16 ring.  The
 * ring selects a table entry {int16 ax, int16 ay}; the sample is taken at
 * byte offset ((vx*ax) >> 16) * 4 + ((vy*ay) >> 16) * pitch from the
 * corresponding pixel of srcCrop.  The offset is checked against the
 * memory of the whole source bitmap (srcFull), not against the row, so
 * a horizontal overshoot continues on the neighbouring row exactly as in
 * the original.
 */
static inline int RippleOffset(const uint8_t* vp, const uint32_t* table, int pitch)
{
	int vx = (int16_t)(vp[0] | vp[1] << 8), vy = (int16_t)(vp[2] | vp[3] << 8);
	unsigned ring = (unsigned)(vp[4] | vp[5] << 8);
	uint32_t t = table[ring];
	int ax = (int16_t)(t & 0xffff), ay = (int16_t)(t >> 16);
	return ((vx * ax) >> 16) * 4 + ((vy * ay) >> 16) * pitch;
}

// 32-bit copy of the displaced samples; 0 where the sample falls outside srcFull's memory
static void Ripple_Copy(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vec, const uint32_t* table)
{
	const uint8_t *base = srcFull->pixels, *end = srcFull->pixels + (size_t)srcFull->h * srcFull->pitch;
	int y, x;
	for(y = 0; y < srcCrop->h; y++)
	{
		const uint8_t *s = ROW(srcCrop, y), *vp = ROW(vec, y);
		uint32_t* d = (uint32_t*)ROW(dst, y);
		for(x = 0; x < srcCrop->w; x++, vp += 6)
		{
			const uint8_t* p = s + x * 4 + RippleOffset(vp, table, srcFull->pitch);
			d[x] = (p >= base && p < end) ? *(const uint32_t*)p : 0;
		}
	}
}

// The smooth variant samples each neighbour inside the source rectangle,
// using the top four fractional bits and two successive interpolations.
static uint32_t RippleSample(const Bmp_t* full, int x, int y)
{
	if(x < 0 || y < 0 || x >= full->w || y >= full->h)
		return 0;
	return ((const uint32_t*)ROW(full, y))[x];
}
static void Ripple_Smooth(Bmp_t* dst, const Bmp_t* crop, const Bmp_t* full, const Bmp_t* vec, const uint32_t* table)
{
	ptrdiff_t offset = crop->pixels - full->pixels;
	int ox = (int)(offset % full->pitch) / 4, oy = (int)(offset / full->pitch);
	for(int y = 0; y < crop->h; y++)
	{
		const uint8_t* vp = ROW(vec, y);
		uint32_t* out = (uint32_t*)ROW(dst, y);
		for(int x = 0; x < crop->w; x++, vp += 6)
		{
			unsigned ring = (unsigned)(vp[4] | vp[5] << 8);
			uint32_t t = table[ring];
			int dx = (int16_t)(vp[0] | vp[1] << 8) * (int16_t)t;
			int dy = (int16_t)(vp[2] | vp[3] << 8) * (int16_t)(t >> 16);
			int sx = ox + x + (dx >> 16), sy = oy + y + (dy >> 16);
			int fx = ((uint32_t)dx >> 12) & 15, fy = ((uint32_t)dy >> 12) & 15;
			uint32_t a = RippleSample(full, sx, sy), b = RippleSample(full, sx + 1, sy);
			uint32_t c = RippleSample(full, sx, sy + 1), d = RippleSample(full, sx + 1, sy + 1), pixel = 0;
			for(int lane = 0; lane < 32; lane += 8)
			{
				int av = (a >> lane) & 255, bv = (b >> lane) & 255, cv = (c >> lane) & 255, dv = (d >> lane) & 255;
				int top = av + (((bv - av) * fx) >> 4), bottom = cv + (((dv - cv) * fx) >> 4);
				pixel |= (uint32_t)(top + (((bottom - top) * fy) >> 4)) << lane;
			}
			out[x] = pixel;
		}
	}
}

/* Write srcCrop (a view into srcFull) read through the ripple into dst, a
 * picture of the same mode.  0 ok, 0xc the map is not PM_VECDIST or not
 * the view's size, 1 the modes differ.  Only 32-bit pictures are drawn;
 * other matching modes return 0 untouched. */
int Blit_RippleEx(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vecdist, const void* table, int smooth)
{
	if(vecdist->mode != PM_VECDIST || srcCrop->w != vecdist->w || srcCrop->h != vecdist->h)
		return 0xc;
	if(dst->mode != srcCrop->mode)
		return 1;
	if(srcCrop->mode == PM_RGB32 || srcCrop->mode == PM_ARGB32)
	{
		if(smooth)
			Ripple_Smooth(dst, srcCrop, srcFull, vecdist, (const uint32_t*)table);
		else
			Ripple_Copy(dst, srcCrop, srcFull, vecdist, (const uint32_t*)table);
	}
	return 0;
}

int Blit_Ripple(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vecdist, const void* table)
{
	return Blit_RippleEx(dst, srcCrop, srcFull, vecdist, table, 0);
}

// wrap a sample address into the source memory (the blending variants wrap instead of reading 0)
static inline const uint8_t* RippleWrap(const uint8_t* p, const uint8_t* base, const uint8_t* end, size_t size)
{
	while(p < base)
		p += size;
	while(p >= end)
		p -= size;
	return p;
}

// RGB32 over RGB32: d = s + (d - s) * level >> 8 on B, G, R; the top byte takes s's
static void Ripple_Blend1(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vec, const uint32_t* table, int level)
{
	const uint8_t *base = srcFull->pixels, *end;
	size_t size = (size_t)srcFull->h * srcFull->pitch;
	int y, x;
	end = base + size;
	for(y = 0; y < srcCrop->h; y++)
	{
		const uint8_t *s = ROW(srcCrop, y), *vp = ROW(vec, y);
		uint32_t* d = (uint32_t*)ROW(dst, y);
		for(x = 0; x < srcCrop->w; x++, vp += 6)
		{
			const uint8_t* p = RippleWrap(s + x * 4 + RippleOffset(vp, table, srcFull->pitch), base, end, size);
			uint32_t sv = *(const uint32_t*)p, dv = d[x];
			int i, out = (int)(sv & 0xff000000u); // lane 3 multiplies by 0
			for(i = 0; i < 24; i += 8)
			{
				int sc = (int)((sv >> i) & 0xff), dc = (int)((dv >> i) & 0xff);
				out |= Sat8((((dc - sc) * level) >> 8) + sc) << i;
			}
			d[x] = (uint32_t)out;
		}
	}
}

// ARGB32 source over RGB32: d += (s - d) * w >> 8 with w = (a * inv) >> 8, pixels with alpha 0 skipped
static void Ripple_Blend2(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vec, const uint32_t* table, int level)
{
	const uint8_t *base = srcFull->pixels, *end;
	size_t size = (size_t)srcFull->h * srcFull->pitch;
	int inv = 0x100 - level, y, x;
	end = base + size;
	for(y = 0; y < srcCrop->h; y++)
	{
		const uint8_t *s = ROW(srcCrop, y), *vp = ROW(vec, y);
		uint32_t* d = (uint32_t*)ROW(dst, y);
		for(x = 0; x < srcCrop->w; x++, vp += 6)
		{
			const uint8_t* p = RippleWrap(s + x * 4 + RippleOffset(vp, table, srcFull->pitch), base, end, size);
			uint32_t sv = *(const uint32_t*)p, dv = d[x];
			int a = (int)(sv >> 24), w, i, out = 0;
			if(!a)
				continue;
			w = (a * inv) >> 8;
			out = (int)(dv & 0xff000000u);
			for(i = 0; i < 24; i += 8)
			{
				int sc = (int)((sv >> i) & 0xff), dc = (int)((dv >> i) & 0xff);
				out |= Sat8((((sc - dc) * w) >> 8) + dc) << i;
			}
			d[x] = (uint32_t)out;
		}
	}
}

/* Blend srcCrop read through the ripple onto an RGB32 destination at
 * transparency `level` (0 .. 0x100).  0 ok, 0xc bad map, 0xf the
 * destination is not RGB32; a source that is not 32-bit returns 0
 * untouched. */
int Blit_RippleLevel(Bmp_t* dst, const Bmp_t* srcCrop, const Bmp_t* srcFull, const Bmp_t* vecdist, const void* table, int level)
{
	if(vecdist->mode != PM_VECDIST || srcCrop->w != vecdist->w || srcCrop->h != vecdist->h)
		return 0xc;
	if(srcCrop->mode == PM_RGB32)
	{
		if(dst->mode != PM_RGB32)
			return 0xf;
		Ripple_Blend1(dst, srcCrop, srcFull, vecdist, (const uint32_t*)table, level);
	}
	else if(srcCrop->mode == PM_ARGB32)
	{
		if(dst->mode != PM_RGB32)
			return 0xf;
		Ripple_Blend2(dst, srcCrop, srcFull, vecdist, (const uint32_t*)table, level);
	}
	return 0;
}
