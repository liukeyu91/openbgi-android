/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * font.c - glyph rasteriser, glyph cache and font cache (inc/bgi/gfx/font.h)
 *
 * The original draws each character with GDI into an 8-bit DIB whose
 * palette runs from white (index 0) to black (255), at gAaScale times the
 * requested size, and then averages gAaScale x gAaScale blocks into a
 * (2*size) x size coverage cell.  Only the GDI call itself is delegated to
 * the OS layer (OS_FontRender); the oversampling, the box filter, the
 * cell layout, the ink-extent metrics and the LRU cache are reproduced
 * here exactly.  The painting helpers at the end turn a coverage cell into
 * pixels of a bitmap in the three screen formats.
 */
#include "bgi/gfx/font.h"
#include "bgi/gfx/bmpops.h" // FontNames_Resolve
#include "bgi/strutil.h"    // TextPutChar

// ---- antialiasing parameters ("90 0D") ---------------------------------------------------------
int gAaShift = 0;
int gAaScale = 1;
int gAaRowShift = 0;

/* "90 0D": level 0..3 selects the oversampling (2, 4, 8 or 16 samples per
 * axis); levels above 3 are ignored (0), negative levels switch
 * oversampling off.  1 when applied.  The globals are read both when a
 * font is created (the buffer size) and when a glyph is rendered (the
 * filter), so a change does not apply cleanly to fonts already open. */
int Gfx_SetAntialias(int level)
{
	if(level > 3)
		return 0;
	switch(level)
	{
		case 0:
			gAaShift = 2;
			gAaScale = 2;
			gAaRowShift = 1;
			break;
		case 1:
			gAaShift = 4;
			gAaScale = 4;
			gAaRowShift = 2;
			break;
		case 2:
			gAaShift = 6;
			gAaScale = 8;
			gAaRowShift = 3;
			break;
		case 3:
			gAaShift = 8;
			gAaScale = 16;
			gAaRowShift = 4;
			break;
		default:
			gAaShift = 0;
			gAaScale = 1;
			gAaRowShift = 0;
			break; // level < 0
	}
	return 1;
}

// ---- FontRaster --------------------------------------------------------------------------------

FontRaster_t* FontRaster_New(void)
{
	FontRaster_t* f = (FontRaster_t*)BGI_Alloc(sizeof *f);
	memset(f, 0, sizeof *f);
	f->pool = (uint8_t*)BGI_Alloc(2); // placeholders, replaced by init
	f->slots = (GlyphSlot_t*)BGI_Alloc(2 * sizeof(GlyphSlot_t));
	return f;
}

// release the OS font and the oversampling buffer; the cells stay allocated
void FontRaster_Close(FontRaster_t* f)
{
	if(!f->open)
		return;
	OS_FontClose(f->os);
	f->os = NULL;
	BGI_Free(f->ss);
	BGI_Free(f->source);
	f->ss = NULL;
	f->source = NULL;
	f->open = 0;
}

void FontRaster_Delete(FontRaster_t* f)
{
	FontRaster_Close(f);
	BGI_Free(f->pool);
	BGI_Free(f->slots);
	BGI_Free(f);
}

// the glyph cells and the cache slots for `capacity` glyphs of `size`
static void FontRaster_AllocCells(FontRaster_t* f, int size, int capacity)
{
	int i;
	f->cellW = size * 2;
	f->cellBytes = f->cellW * size;
	BGI_Free(f->pool);
	f->pool = (uint8_t*)BGI_Alloc((size_t)f->cellBytes * (size_t)capacity);
	BGI_Free(f->slots);
	f->slots = (GlyphSlot_t*)BGI_Alloc((size_t)capacity * sizeof(GlyphSlot_t));
	f->capacity = capacity;
	f->used = 0;
	f->lru = NULL;
	for(i = 0; i < capacity; i++)
	{
		memset(&f->slots[i], 0, sizeof f->slots[i]);
		f->slots[i].g.pixels = f->pool + (size_t)i * f->cellBytes;
	}
}

/* open the OS font at gAaScale times the size, with the average
 * (half-width) character width gAaScale * size * widthPct / 200 so that a
 * full-width glyph fills the whole cell, and allocate the oversampling
 * buffer of renderW * gAaScale x gAaScale * size bytes (rows dword
 * aligned).  1 ok, 0 when the OS refuses the font. */
static void FontRaster_SetTracking(FontRaster_t* f, const FontFaceExtension_t* ext)
{
	int32_t scale = ext->sx ? ext->sx : ext->sy ? ext->sy : 0x10000;
	int width = f->glyphW * scale / 0x10000;
	f->tracking = (int)((int64_t)width * ext->spacing / 0x10000);
}

static int FontRaster_CreateFont(FontRaster_t* f, const char* face, int size, int widthPct, int bold, int italic,
	const FontFaceExtension_t* ext)
{
	int scaledW, scaledH, nominalWidth;
	int32_t sx = 0x10000, sy = 0x10000;
	nominalWidth = (gAaScale * size * widthPct) / 200;
	if(ext && ext->sy)
	{
		sx = ext->sx ? ext->sx : ext->sy;
		sy = ext->sy;
	}
	f->glyphW = (size * widthPct) / 100;
	// an italic font gets a DIB a quarter wider for the shear
	f->renderW = italic ? (size * widthPct * 5) / 400 : f->glyphW;
	if(f->renderW > f->cellW)
		f->renderW = f->cellW;
	f->ssH = gAaScale * size;
	f->ssW = f->renderW * gAaScale;
	scaledW = f->ssW * sx / 0x10000;
	scaledH = f->ssH * sy / 0x10000;
	f->cropX = ext ? (int)((int64_t)(f->ssH * sx / 0x10000) * ext->dx / 0x10000) : 0;
	f->cropY = ext ? (int)((int64_t)scaledH * ext->dy / 0x10000) : 0;
	f->os = OS_FontOpen(face, scaledH, ext && ext->sy && !ext->sx ? 0 : nominalWidth * sx / 0x10000, bold, italic);
	if(!f->os)
		return 0;
	f->ssPitch = (f->ssW + 3) & ~3; // DIB rows are dword aligned
	f->ssBytes = f->ssPitch * f->ssH;
	f->ss = (uint8_t*)BGI_Alloc((size_t)(f->ssBytes ? f->ssBytes : 1));
	f->sourceW = scaledW;
	f->sourceH = scaledH;
	f->sourcePitch = (scaledW + 3) & ~3;
	f->source = NULL;
	if(scaledW != f->ssW || scaledH != f->ssH || f->cropX || f->cropY)
		f->source = (uint8_t*)BGI_Alloc((size_t)f->sourcePitch * f->sourceH);
	f->tracking = 0;
	if(ext)
		FontRaster_SetTracking(f, ext);
	f->size = size;
	f->widthPct = widthPct;
	f->bold = bold;
	f->italic = italic;
	return 1;
}

/* (re)open the rasteriser: `capacity` glyphs are cached.  0 ok, 0x80000001
 * capacity < 2, 0x80000002 size not 4..200, 0x80000003 width not 25..200,
 * 0x80000004 the font could not be created. */
static int FontRaster_InitExtension(FontRaster_t* f, const char* face, int size, int widthPct, int bold, int italic,
	int capacity, const FontFaceExtension_t* ext)
{
	FontRaster_Close(f);
	if(capacity < 2)
		return (int)0x80000001;
	if(size < 4 || size > 200)
		return (int)0x80000002;
	if(widthPct < 25 || widthPct > 200)
		return (int)0x80000003;
	FontRaster_AllocCells(f, size, capacity);
	if(!FontRaster_CreateFont(f, face, size, widthPct, bold, italic, ext))
		return (int)0x80000004;
	f->open = 1;
	return 0;
}

int FontRaster_Init(FontRaster_t* f, const char* face, int size, int widthPct, int bold, int italic, int capacity)
{
	return FontRaster_InitExtension(f, face, size, widthPct, bold, italic, capacity, NULL);
}

static void FontRaster_Draw(FontRaster_t* f, const char* str, int len)
{
	memset(f->ss, 0, (size_t)f->ssBytes);
	if(!f->source)
	{
		OS_FontRender(f->os, str, len, f->ss, f->ssPitch, f->ssW, f->ssH);
		return;
	}
	int x = f->cropX < 0 ? -f->cropX : 0;
	int y = f->cropY < 0 ? -f->cropY : 0;
	int right = BGI_MIN(f->ssW, f->sourceW - f->cropX);
	int bottom = BGI_MIN(f->ssH, f->sourceH - f->cropY);
	if(x >= right || y >= bottom)
		return;
	memset(f->source, 0, (size_t)f->sourcePitch * f->sourceH);
	OS_FontRender(f->os, str, len, f->source, f->sourcePitch, f->sourceW, f->sourceH);
	for(; y < bottom; ++y)
		memcpy(f->ss + (size_t)y * f->ssPitch + x,
			f->source + (size_t)(y + f->cropY) * f->sourcePitch + (x + f->cropX), (size_t)(right - x));
}

/* draw one character through the OS layer and reduce it into `cell`
 * (box filter of gAaScale x gAaScale samples, or a plain copy without
 * oversampling); the columns right of renderW are cleared.  Fills the ink
 * extent into g->left .. g->bottom. */
static void FontRaster_Render(FontRaster_t* f, Glyph_t* g, uint8_t* cell, int code)
{
	char str[8];
	int len, x, y, size = f->size, cw = f->cellW, gw = f->renderW;

	// the character's bytes in the text encoding (Shift-JIS, or UTF-8 from "81 00")
	len = TextPutChar(str, (uint32_t)code);
	str[len] = 0;
	FontRaster_Draw(f, str, len);

	if(code == 0x20 || code == (gTextBig5 ? 0xa140 : gTextGbk ? 0xa1a1 : gTextUtf8 ? 0x3000 : 0x8140))
	{ // spaces: blank cell, half advance
		memset(cell, 0, (size_t)f->cellBytes);
		g->left = 0;
		g->top = 0;
		g->right = size / 2 - 1;
		g->bottom = size - 1;
		return;
	}

	if(gAaScale > 1)
	{
		const uint8_t* rowBase = f->ss;
		uint8_t* d = cell;
		for(y = 0; y < size; y++)
		{
			const uint8_t* col = rowBase;
			for(x = 0; x < gw; x++)
			{
				const uint8_t* p = col;
				int sum = 0, i, j;
				for(j = 0; j < gAaScale; j++, p += f->ssPitch)
					for(i = 0; i < gAaScale; i++)
						sum += p[i];
				*d++ = (uint8_t)(sum >> gAaShift);
				col += gAaScale;
			}
			for(; x < cw; x++)
				*d++ = 0;
			rowBase += f->ssPitch << gAaRowShift;
		}
	}
	else
	{
		const uint8_t* s = f->ss;
		uint8_t* d = cell;
		for(y = 0; y < size; y++)
		{
			memcpy(d, s, (size_t)gw);
			memset(d + gw, 0, (size_t)(cw - gw));
			d += cw;
			s += f->ssPitch;
		}
	}

	// ink extent: first and last column with a non-zero sample.  The
	// original leaves both uninitialised for a glyph without ink; a full
	// blank cell is used here.
	g->left = 0;
	g->right = f->glyphW - 1;
	g->top = 0;
	g->bottom = size - 1;
	for(x = 0; x < gw; x++)
	{
		for(y = 0; y < size; y++)
			if(cell[y * cw + x])
				break;
		if(y < size)
		{
			g->left = x;
			break;
		}
	}
	for(x = gw - 1; x >= 0; x--)
	{
		for(y = 0; y < size; y++)
			if(cell[y * cw + x])
				break;
		if(y < size)
		{
			g->right = x;
			break;
		}
	}
}

/* fetch the glyph of `code`, rendering it on a miss.  The slots form a
 * singly linked list from `lru`: a hit moves the slot to the front, a miss
 * takes a fresh slot while there are any and otherwise recycles the last
 * one visited.  *out receives a copy of the glyph record (its `pixels`
 * point into the cache and stay valid until the slot is recycled); the
 * result is `out`. */
const Glyph_t* FontRaster_Glyph(FontRaster_t* f, Glyph_t* out, int code)
{
	GlyphSlot_t *prev = NULL, *s = f->lru;
	int i;
	for(i = 0; i < f->used && s; i++)
	{
		if(s->g.code == code)
			break;
		prev = s;
		s = s->next;
	}
	if(i < f->used && s)
	{ // hit: unlink
		if(prev)
			prev->next = s->next;
		else
			f->lru = s->next;
	}
	else
	{
		if(f->used < f->capacity)
			s = &f->slots[f->used++];
		else
		{
			s = prev; // the tail becomes the new head
			if(!s)
				s = &f->slots[0];
		}
		s->g.code = code;
		s->g.dbcs = code >= 0x100;
		FontRaster_Render(f, &s->g, s->g.pixels, code);
	}
	s->next = f->lru;
	f->lru = s;
	*out = s->g;
	return out;
}

/* the font's own advance of `code` (see font.h): the OS layer measures at
 * the oversampled size, so the value is scaled back down */
int FontRaster_Advance(FontRaster_t* f, int code)
{
	char str[8];
	int len;
	if(!f || !f->os)
		return 0;
	len = TextPutChar(str, (uint32_t)code);
	str[len] = 0;
	return OS_FontCharAdvance(f->os, str, len) / gAaScale;
}

// ---- FontCache ---------------------------------------------------------------------------------

typedef struct FontFaceRule
{
	char name[0x60];
	FontFaceExtension_t ext;
	struct FontFaceRule* next;
} FontFaceRule_t;

FontCache_t* FontCache_New(void)
{
	FontCache_t* c = (FontCache_t*)BGI_Alloc(sizeof *c);
	memset(c, 0, sizeof *c);
	c->defaultAmount = 0x40;
	return c;
}

// close every open font and drop the requests
void FontCache_Delete(FontCache_t* c)
{
	FontReq_t* r = c->requests;
	FontEntry_t* e = c->entries;
	while(r)
	{
		FontReq_t* n = r->next;
		BGI_Free(r);
		r = n;
	}
	while(e)
	{
		FontEntry_t* n = e->next;
		if(e->font)
			FontRaster_Delete(e->font);
		BGI_Free(e);
		e = n;
	}
	while(c->faceRules)
	{
		FontFaceRule_t* rule = c->faceRules;
		c->faceRules = rule->next;
		BGI_Free(rule);
	}
	BGI_Free(c);
}

/* the original compares face names with its own two-bytes-at-a-time
 * strcmp; equality is all that matters */
static int SameFace(const char* a, const char* b)
{
	return strcmp(a, b) == 0;
}

static FontFaceRule_t* FindFaceRule(FontCache_t* c, const char* name)
{
	for(FontFaceRule_t* rule = c ? c->faceRules : NULL; rule; rule = rule->next)
		if(SameFace(rule->name, name))
			return rule;
	return NULL;
}

int FontCache_InitRaster(FontCache_t* c, FontRaster_t* f, const char* name, int size, int widthPct, int bold,
	int italic, int capacity)
{
	FontFaceRule_t* rule = FindFaceRule(c, name);
	return FontRaster_InitExtension(f, FontNames_Resolve(name), size, widthPct, bold, italic, capacity,
		rule ? &rule->ext : NULL);
}

int FontCache_SetFaceSpacing(FontCache_t* c, const char* face, int32_t spacing)
{
	FontFaceRule_t* rule;
	if(spacing < -0x10000 || spacing > 0x10000)
		return 7;
	rule = FindFaceRule(c, face);
	if(!rule)
		return 8;
	rule->ext.spacing = spacing;
	for(FontEntry_t* e = c->entries; e; e = e->next)
		if(SameFace(e->name, face))
			FontRaster_SetTracking(e->font, &rule->ext);
	return 0;
}

int FontCache_SetFaceExtension(FontCache_t* c, const char* face, const FontFaceExtension_t* ext)
{
	FontFaceRule_t* rule;
	int32_t sx = ext->sx ? ext->sx : ext->sy;
	if(ext->spacing < -0x10000 || ext->spacing > 0x10000)
		return 7;
	if(sx || ext->sy || ext->dx || ext->dy)
	{
		if(sx < 0x10000 || sx > 0x20000 || ext->sy < 0x10000 || ext->sy > 0x20000)
			return 5;
		if(ext->dx > (int32_t)(65536.0 - 4294967296.0 / sx) ||
			ext->dy > (int32_t)(65536.0 - 4294967296.0 / ext->sy))
			return 6;
	}
	if(!*face || strlen(face) >= sizeof rule->name)
		return 4;
	rule = FindFaceRule(c, face);
	if(rule && rule->ext.sx == ext->sx && rule->ext.sy == ext->sy &&
		rule->ext.dx == ext->dx && rule->ext.dy == ext->dy)
		return FontCache_SetFaceSpacing(c, face, ext->spacing);
	if(!rule)
	{
		rule = (FontFaceRule_t*)BGI_Calloc(sizeof *rule);
		strcpy(rule->name, face);
		rule->next = c->faceRules;
		c->faceRules = rule;
	}
	rule->ext = *ext;
	for(FontEntry_t* e = c->entries; e; e = e->next)
	{
		if(!SameFace(e->name, face))
			continue;
		FontRaster_t* f = e->font;
		FontRaster_t updated = *f;
		if(FontRaster_CreateFont(&updated, FontNames_Resolve(face), f->size, f->widthPct, f->bold, f->italic, ext))
		{
			FontRaster_Close(f);
			*f = updated;
			f->used = 0;
			f->lru = NULL;
		}
	}
	return 0;
}

/* "90 0E": remember that `amount` glyphs are to be cached for the font
 * (face, size, width, bold); a request for the same font is updated.
 * With a name, the parameters are checked first: 0x80000002 size not
 * 4..200, 0x80000003 width not 25..200, 0x80000001 amount < 2.  Without a
 * name a request with an empty name is added unchecked.  0 ok. */
int FontCache_Request(FontCache_t* c, const char* name, int size, int widthPct, int bold, int amount)
{
	FontReq_t* r;
	if(name)
	{
		if(size < 4 || size > 200)
			return (int)0x80000002;
		if(widthPct < 25 || widthPct > 200)
			return (int)0x80000003;
		if(amount < 2)
			return (int)0x80000001;
		for(r = c->requests; r; r = r->next)
		{
			if(SameFace(name, r->name) && r->size == size && r->widthPct == widthPct && ((bold != 0) == (r->bold != 0)))
			{
				r->amount = amount;
				return 0;
			}
		}
	}
	r = (FontReq_t*)BGI_Alloc(sizeof *r);
	memset(r, 0, sizeof *r);
	if(name)
		strncpy(r->name, name, sizeof r->name - 1);
	r->size = size;
	r->widthPct = widthPct;
	r->bold = bold;
	r->amount = amount;
	r->next = c->requests;
	c->requests = r;
	return 0;
}

// the glyph amount requested for a font, or the default (0x40)
int FontCache_AmountFor(FontCache_t* c, const char* name, int size, int widthPct, int bold)
{
	FontReq_t* r;
	for(r = c->requests; r; r = r->next)
		if(SameFace(name, r->name) && r->size == size && r->widthPct == widthPct && ((bold != 0) == (r->bold != 0)))
			return r->amount;
	return c->defaultAmount;
}

/* find the open font (face, size, width, bold) or open it (the face name
 * resolved through FontNames_Resolve, the glyph amount from the requests)
 * and append it to the list; *handle receives its number, one above the
 * last entry's.  0 ok, or the FontRaster_Init error (*handle is 0 then). */
int FontCache_Open(FontCache_t* c, int* handle, const char* name, int size, int widthPct, int bold)
{
	FontEntry_t *e, *last = NULL;
	FontRaster_t* f;
	int r;
	*handle = 0;
	for(e = c->entries; e; last = e, e = e->next)
	{
		if(SameFace(name, e->name) && e->size == size && e->widthPct == widthPct && e->bold == bold)
		{
			*handle = e->handle;
			return 0;
		}
	}
	f = FontRaster_New();
	r = FontCache_InitRaster(c, f, name, size, widthPct, bold, 0, FontCache_AmountFor(c, name, size, widthPct, bold));
	if(r != 0)
	{
		FontRaster_Delete(f); // the original leaks it
		return r;
	}
	e = (FontEntry_t*)BGI_Alloc(sizeof *e);
	memset(e, 0, sizeof *e);
	e->handle = (last ? last->handle : c->lastHandle) + 1;
	strncpy(e->name, name, sizeof e->name - 1);
	e->size = size;
	e->widthPct = widthPct;
	e->bold = bold;
	e->font = f;
	e->next = NULL;
	if(last)
		last->next = e;
	else
		c->entries = e;
	*handle = e->handle;
	return 0;
}

// copy the entry of `handle` into *out (italic 0); 1 found, 0 unknown handle
int FontCache_GetInfo(FontCache_t* c, FontInfo_t* out, int handle)
{
	FontEntry_t* e;
	for(e = c->entries; e; e = e->next)
	{
		if(e->handle == handle)
		{
			memcpy(out->name, e->name, sizeof out->name);
			out->size = e->size;
			out->widthPct = e->widthPct;
			out->bold = e->bold;
			out->italic = 0;
			out->font = e->font;
			return 1;
		}
	}
	return 0;
}

// "91 0F" (1.529 on): FontCache_Open, then the two extra widths stored on the rasteriser
int FontCache_OpenExtra(FontCache_t* c, int* handle, const char* name, int size, int widthPct, int bold, int extraUpright,
	int extraItalic)
{
	FontInfo_t fi;
	int r = FontCache_Open(c, handle, name, size, widthPct, bold);
	if(r == 0 && FontCache_GetInfo(c, &fi, *handle))
	{
		fi.font->extraW[0] = extraUpright;
		fi.font->extraW[1] = extraItalic;
	}
	return r;
}

// ---- glyph painting ----------------------------------------------------------------------------

// narrow a cell view to the inked columns of the glyph
void Bmp_CropGlyph(Bmp_t* b, const Glyph_t* g)
{
	b->w = g->right - g->left + 1;
	b->pixels += b->bpp * g->left;
}

/* fetch the glyph of `code` into *g and paint it into the top-left of
 * `cell` (clipped to the cell and to the font's cell size) in `colour`:
 * ARGB32 gets the colour with the coverage as alpha, RGB32 a 0/1 mask,
 * RGB16 the 5-5-5 colour scaled by the coverage; other modes are left
 * alone. */
void Font_PaintGlyph(Bmp_t* cell, Glyph_t* g, int code, FontRaster_t* font, uint32_t colour)
{
	int rows, cols, y, x;
	const uint8_t* src;
	FontRaster_Glyph(font, g, code);
	rows = cell->h < font->size ? cell->h : font->size;
	cols = cell->w < font->cellW ? cell->w : font->cellW;
	src = g->pixels;
	switch(cell->mode)
	{
		case PM_ARGB32:
		{ // colour with the coverage as alpha
			uint32_t rgb = colour & 0xffffffu;
			for(y = 0; y < rows; y++, src += font->cellW)
			{
				uint32_t* d = (uint32_t*)(cell->pixels + (size_t)y * cell->pitch);
				for(x = 0; x < cols; x++)
					d[x] = ((uint32_t)src[x] << 24) | rgb;
			}
			break;
		}
		case PM_RGB32:
		{ // 0/1 mask: any channel * coverage >= 256
			int r = (int)((colour >> 16) & 0xff), gch = (int)((colour >> 8) & 0xff), b = (int)(colour & 0xff);
			for(y = 0; y < rows; y++, src += font->cellW)
			{
				uint32_t* d = (uint32_t*)(cell->pixels + (size_t)y * cell->pitch);
				for(x = 0; x < cols; x++)
				{
					int c = src[x];
					d[x] = ((r * c) & 0xffff00) || ((gch * c) & ~0xff) || ((b * c) & ~0xff) ? 1u : 0u;
				}
			}
			break;
		}
		case PM_RGB16:
		{ // 5-5-5 colour scaled by the coverage
			int r = (int)((colour >> 16) & 0xff), gch = (int)((colour >> 8) & 0xff), b = (int)(colour & 0xff);
			for(y = 0; y < rows; y++, src += font->cellW)
			{
				uint16_t* d = (uint16_t*)(cell->pixels + (size_t)y * cell->pitch);
				for(x = 0; x < cols; x++)
				{
					int c = src[x];
					unsigned v = (((unsigned)(c * r) >> 1) & 0xfc1f) | ((unsigned)(c * gch) >> 6);
					v = (v & 0xffe0) | ((unsigned)(c * b) >> 11);
					d[x] = (uint16_t)v;
				}
			}
			break;
		}
		default:
			break;
	}
}

int Font_GlyphCellW(const Glyph_t* g, int fullWidth, int wide)
{
	int halfWidth = fullWidth / 2;
	if(wide)
		return fullWidth;
	/* CJK fallback fonts can draw Latin wider than half an em, regardless
	 * of the text encoding. Keep that ink and one trailing blank column. */
	if(g->code >= '!' && g->code <= '~' && g->right >= halfWidth - 1)
		return g->right + 2;
	return halfWidth;
}

/* horizontal gap added on both sides of a proportionally drawn glyph,
 * from the ratio of its ink width to the font size: 4 pixels for the
 * narrowest glyphs down to 1 for those at least half the size wide */
int Font_GapFor(const Glyph_t* g, int size)
{
	static const int table[8] = {4, 4, 4, 3, 3, 3, 3, 2};
	int idx = ((g->right - g->left + 1) << 4) / size;
	return idx < 8 ? table[idx] : 1;
}

/* the variant used by the rich layout and Text_Measure: size / 16 + 1,
 * plus a share that grows for narrow glyphs (at most size / 2) */
int Font_GapFor2(const Glyph_t* g, int size)
{
	int q = (size * 4) / ((g->right - g->left) * 2 + 2) - 2;
	if(q <= 0)
		q = 0;
	else if(q > (size >> 1))
		q = size >> 1;
	return (size >> 4) + q + 1;
}

int Font_Percent(int a, int pct) // a * pct / 100
{
	return (a * pct) / 100;
}
