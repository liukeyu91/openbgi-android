/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * bmseq.c - BF_Movie sequence records (inc/bgi/gfx/bmseq.h): the list of
 * loaded movies behind "90 F4 .. F7", their clones and the frame decode
 * into a bitmap slot
 *
 * The original keeps a critical section per record and one for the list
 * because the decoder pool and the loader thread touch them; here every
 * call runs on the engine thread.
 */
#include "bgi/gfx/bmseq.h"
#include "bgi/gfx/bmpmgr.h"
#include "bgi/bfmovie.h"
#include "bgi/file.h"

typedef struct BmSeqData
{
	uint8_t* index;   // the whole file, or just the header, tables and offsets
	uint32_t size;    // the full file size
	FileView_t view;
	uint32_t readOffset;
	uint8_t* frame;
	uint32_t capacity;
	unsigned refs;
} BmSeqData_t;

typedef struct BmSeq
{
	int32_t no;         // the number the scripts use (from gCounter)
	BmSeqData_t* data;
	int32_t cloneOf;    // the record this one was cloned from (0: none)
	int32_t clone;      // the clone made of this one (0: none)
	struct BmSeq* next; // the list, newest first
} BmSeq_t;

static BmSeq_t* gList;   // every loaded movie
static int32_t gCounter; // the last number handed out (numbers start at 1)
static int gDeferFlag;   // "80 53": the next decode goes through a wait object

// the record with number `no`, NULL when there is none
static BmSeq_t* Find(int32_t no)
{
	BmSeq_t* s;
	for(s = gList; s; s = s->next)
		if(s->no == no)
			return s;
	return NULL;
}

static void Register(BmSeqData_t* data, int32_t* outNo, int32_t info[5])
{
	const BfMovieHeader_t* h = (const BfMovieHeader_t*)data->index;
	BmSeq_t* s = (BmSeq_t*)BGI_Calloc(sizeof *s);
	s->no = ++gCounter;
	s->data = data;
	data->refs = 1;
	s->next = gList;
	gList = s;
	*outNo = s->no;
	info[0] = (int32_t)h->width;
	info[1] = (int32_t)h->height;
	info[2] = (int32_t)h->mode;
	info[3] = (int32_t)h->fps;
	info[4] = (int32_t)h->frames;
}

uint32_t BmSeq_Register(const void* file, uint32_t size, int32_t* outNo, int32_t info[5])
{
	BmSeqData_t* data;
	if(!BfMovie_Check(file, size))
		return 0x80000002u;
	data = (BmSeqData_t*)BGI_Calloc(sizeof *data);
	File_Ctor(&data->view.file);
	data->size = size;
	data->index = (uint8_t*)BGI_Alloc(size);
	memcpy(data->index, file, size);
	Register(data, outNo, info);
	return 0;
}

int BmSeq_TryStream(const char* arc, const char* name, int32_t* outNo, int32_t info[5])
{
	FileView_t view;
	BfMovieHeader_t header;
	const uint32_t headerSize = (uint32_t)offsetof(BfMovieHeader_t, offset);
	uint32_t indexSize = 0;
	uint8_t* index;
	BmSeqData_t* data;
	if(!LoadFile_OpenView(&view, arc, name))
		return 0;
	if(view.size >= headerSize && File_Read(&view.file, &header, headerSize) == headerSize)
		indexSize = BfMovie_StreamIndexSize(&header, view.size);
	if(!indexSize)
	{
		File_Close(&view.file);
		return 0;
	}
	index = (uint8_t*)BGI_Alloc(indexSize);
	memcpy(index, &header, headerSize);
	if(File_Read(&view.file, index + headerSize, indexSize - headerSize) != indexSize - headerSize)
	{
		BGI_Free(index);
		File_Close(&view.file);
		return 0;
	}
	data = (BmSeqData_t*)BGI_Calloc(sizeof *data);
	data->size = view.size;
	data->index = index;
	data->view = view;
	data->readOffset = indexSize;
	Register(data, outNo, info);
	return 1;
}

/* Drop record `no`.  Its partner (original or clone) is unlinked and the
 * file data is freed with the last of the two.  0 ok, 0x80000003 no such
 * number. */
uint32_t BmSeq_Free(int32_t no)
{
	BmSeq_t *s, *prev = NULL, *o;
	for(s = gList; s && s->no != no; s = s->next)
		prev = s;
	if(!s)
		return 0x80000003u;
	if(prev)
		prev->next = s->next;
	else
		gList = s->next;
	// unlink the partner; the data goes with the last of the pair
	if(s->cloneOf && (o = Find(s->cloneOf)) != NULL)
		o->clone = 0;
	if(s->clone && (o = Find(s->clone)) != NULL)
		o->cloneOf = 0;
	if(--s->data->refs == 0)
	{
		File_Close(&s->data->view.file);
		BGI_Free(s->data->frame);
		BGI_Free(s->data->index);
		BGI_Free(s->data);
	}
	BGI_Free(s);
	return 0;
}

/* Give record `no` a second number (*outNo) that shares its data.  0 ok,
 * 0x80000003 no such number, 0x80000007 it already has a clone. */
uint32_t BmSeq_Clone(int32_t no, int32_t* outNo)
{
	BmSeq_t *s = Find(no), *c;
	if(!s)
		return 0x80000003u;
	if(s->clone)
		return 0x80000007u;
	c = (BmSeq_t*)BGI_Alloc(sizeof *c);
	memset(c, 0, sizeof *c);
	c->no = ++gCounter;
	c->data = s->data;
	c->data->refs++;
	c->cloneOf = no;
	c->next = gList;
	gList = c;
	s->clone = c->no;
	*outNo = c->no;
	return 0;
}

static int DecodeStream(BmSeqData_t* data, uint32_t frame, const Bmp_t* bmp)
{
	uint32_t start, size = BfMovie_FrameRange(data->index, data->size, frame, &start);
	if(!size || (start != data->readOffset && !File_Seek(&data->view.file, data->view.offset + start)))
		return 0;
	if(size > data->capacity)
	{
		BGI_Free(data->frame);
		data->frame = (uint8_t*)BGI_Alloc(size);
		data->capacity = size;
	}
	if(File_Read(&data->view.file, data->frame, size) != size)
	{
		data->readOffset = UINT32_MAX;
		return 0;
	}
	data->readOffset = start + size;
	return BfMovie_DecodeStreamed(data->index, data->frame, size, bmp->pixels, bmp->pitch);
}

/* Decode frame `frame` of movie `no` into bitmap slot bmpNo, which must
 * hold a 32-bit bitmap of the movie's width, height and pixel mode.  0 ok,
 * 0x80000003 no such number, 0x80000004 no such frame, 0x80000005 the
 * slot does not match, 0x8000000A an unsupported movie type (reported once on stderr), 0x80000008 the decoder failed. */
uint32_t BmSeq_Decode(int bmpNo, int32_t no, uint32_t frame)
{
	BmSeq_t* s = Find(no);
	const BfMovieHeader_t* h;
	Bmp_t b;
	if(!s)
		return 0x80000003u;
	h = (const BfMovieHeader_t*)s->data->index;
	if(frame >= h->frames)
		return 0x80000004u;
	if(!BmpMgr_GetInfo(gBmpMgr, &b, bmpNo) || (uint32_t)b.w != h->width || (uint32_t)b.h != h->height ||
		(uint32_t)b.mode != h->mode || b.bpp != 4)
		return 0x80000005u;
	if(!BfMovie_CanDecode(h))
	{
		static int warned;
		if(!warned)
		{
			fprintf(stderr, "bgi: BF_Movie type 0x%x is not decoded by this build\n",
				(unsigned)h->type);
			warned = 1;
		}
		return 0x8000000au;
	}
	if(!(s->data->view.file.h != OS_INVALID_FILE ? DecodeStream(s->data, frame, &b)
	    : BfMovie_DecodeFrame(h, s->data->size, frame, b.pixels, b.pitch)))
		return 0x80000008u; // (the pool's "decode failed")
	return 0;
}

// drop every record (engine shutdown)
void BmSeq_FreeAll(void)
{
	while(gList)
		BmSeq_Free(gList->no);
}

void BmSeq_SetDeferFlag(int on)
{
	gDeferFlag = on;
}

// read and clear the flag
int BmSeq_TakeDeferFlag(void)
{
	int f = gDeferFlag;
	gDeferFlag = 0;
	return f;
}
