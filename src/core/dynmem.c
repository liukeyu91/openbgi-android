/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * dynmem.c - the global memory, the dynamic global blocks and the
 *            resolution of tagged script pointers (inc/bgi/sys.h)
 *
 * Scripts address memory through 32-bit tagged values (inc/bgi/version.h:
 * tag << VM_TAG_SHIFT | offset).  Tag 0 is the global memory block that
 * "80 70" sizes and the save files hold; the running thread's code, data
 * and heap areas have their own tags; the tags from gEngine->dynFirst on
 * are the dynamic blocks a script allocates with "80 20" and frees with
 * "80 21".  ResolvePtr turns such a value into a host pointer and raises
 * the script error for anything unmapped.
 */
#include "bgi/sys.h"
#include "bgi/vm.h"
#include "bgi/error.h"
#include "bgi/msg.h"

uint8_t* gGlobalMem; // tag 0, gGlobalMemSize bytes
uint32_t gGlobalMemSize;
uint8_t* gSysArea; // the system area of "80 82" / "80 83"
uint32_t gSysAreaSize;

static uint8_t* gDynBlocks[VM_DYN_SLOTS_MAX]; // one block per tag from dynFirst (48 slots in 1.69)
static int gDynReady;                         // DynMem_Init has run

/* 1.535 on: the dynamic blocks come from five size-class pools instead of
 * one tag per block.  A pool has `count` slots of at most `maxSize` bytes;
 * a pointer into its slot s is
 * (firstTag + (s >> (26 - bits))) << 26 | (s & ((1 << (26 - bits)) - 1))
 * << bits | offset, so the pools take the tags 0x10 .. 0x3F between them
 * and a 4 KB block costs a 14-bit slot number plus 12 offset bits rather
 * than a tag of its own.  GAlloc takes the first pool whose maximum fits
 * the size and that still has a free slot (a full pool falls through to
 * the next, larger one). */
typedef struct DynPool
{
	uint32_t count, bits, maxSize, firstTag; // slots; offset bits per slot; largest block, bytes; first tag
	uint8_t** blocks;                        // the slots, NULL when free (made on first use)
	uint32_t* sizes;                         // requested allocation sizes for 1.680 scalar operations
} DynPool_t;
static DynPool_t gDynPools[5] = {
	{0x10000, 12, 0x1000, 0x10, NULL, NULL},
	{0x1000, 16, 0x10000, 0x14, NULL, NULL},
	{0x100, 20, 0x100000, 0x18, NULL, NULL},
	{0x10, 24, 0x1000000, 0x1c, NULL, NULL},
	{0x20, 26, 0x4000000, 0x20, NULL, NULL},
};
// 1.680 uses 28-bit offsets, with size classes starting at tags 4, 5, 6, 7, 8 and 12.
static DynPool_t gDynPools680[] = {
	{0x10000, 12, 0x1000, 4, NULL, NULL},
	{0x1000, 16, 0x10000, 5, NULL, NULL},
	{0x100, 20, 0x100000, 6, NULL, NULL},
	{0x10, 24, 0x1000000, 7, NULL, NULL},
	{0x10, 26, 0x4000000, 8, NULL, NULL},
	{0x04, 28, 0x10000000, 12, NULL, NULL},
};
#define DYN_POOLED (gEngine->dynCount == 0) // the profile uses the pools

static DynPool_t* DynPools(size_t* count, unsigned* shift)
{
	if(gEngine->gen == GEN_1_680)
	{
		*count = BGI_COUNTOF(gDynPools680);
		*shift = 28;
		return gDynPools680;
	}
	*count = BGI_COUNTOF(gDynPools);
	*shift = 26;
	return gDynPools;
}

// the pool a tag belongs to, with the slot of (tag, offset); NULL when none
static DynPool_t* DynPool_Of(uint32_t tag, uint32_t off, uint32_t* outSlot)
{
	size_t i, count;
	unsigned shift;
	DynPool_t* pools = DynPools(&count, &shift);
	for(i = 0; i < count; i++)
	{
		DynPool_t* p = &pools[i];
		uint32_t tags = p->count >> (shift - p->bits); // the tags the pool spans
		if(tag >= p->firstTag && tag < p->firstTag + tags)
		{
			*outSlot = ((tag - p->firstTag) << (shift - p->bits)) | (off >> p->bits);
			return p;
		}
	}
	return NULL;
}

/* "80 70": the global memory becomes 0x1000 << shift bytes, shift in
 * 0 .. 12; the old block is freed (its content is lost) and the new one
 * zeroed.  Returns 1 on success, 0 for a shift out of range. */
int SetGlobalMemSize(int shift)
{
	if(shift < 0 || shift > 12)
		return 0;
	gGlobalMemSize = 0x1000u << shift;
	BGI_Free(gGlobalMem);
	gGlobalMem = (uint8_t*)BGI_Calloc(gGlobalMemSize);
	return 1;
}

// "80 71": zero the global memory
void ClearGlobalMem(void)
{
	memset(gGlobalMem, 0, gGlobalMemSize);
}

static void DynPool_Ensure(DynPool_t* p)
{
	if(!p->blocks)
	{
		p->blocks = (uint8_t**)BGI_Calloc(p->count * sizeof(uint8_t*));
		if(gEngine->gen == GEN_1_680)
			p->sizes = (uint32_t*)BGI_Calloc(p->count * sizeof(uint32_t));
	}
}

void DynMem_Init(void)
{
	memset(gDynBlocks, 0, sizeof(gDynBlocks));
	gDynReady = 1;
}

static void ClearPools(DynPool_t* pools, size_t count)
{
	size_t i, j;
	for(i = 0; i < count; i++)
		if(pools[i].blocks)
			for(j = 0; j < pools[i].count; j++)
			{
				BGI_Free(pools[i].blocks[j]);
				pools[i].blocks[j] = NULL;
			}
}

// free every dynamic block (a reboot and the shutdown); the slot tables stay
void DynMem_FreeAll(void)
{
	size_t i;
	if(!gDynReady)
		return;
	for(i = 0; i < gEngine->dynCount; i++)
	{
		BGI_Free(gDynBlocks[i]);
		gDynBlocks[i] = NULL;
	}
	ClearPools(gDynPools, BGI_COUNTOF(gDynPools));
	ClearPools(gDynPools680, BGI_COUNTOF(gDynPools680));
}

/* "80 20": allocate a dynamic block of `size` bytes and return its tagged
 * pointer, 0 when every slot is taken.  The block is zeroed: the original
 * takes it from the process heap as it comes, and the scripts of the later
 * games pass freshly allocated one-byte blocks as empty strings. */
uint32_t GAlloc(uint32_t size)
{
	size_t i, j;
	if(DYN_POOLED)
	{
		size_t count;
		unsigned shift;
		DynPool_t* pools = DynPools(&count, &shift);
		for(i = 0; i < count; i++)
		{
			DynPool_t* p = &pools[i];
			if(size > p->maxSize)
				continue;
			DynPool_Ensure(p);
			for(j = 0; j < p->count; j++)
				if(!p->blocks[j])
				{
					uint32_t slot = (uint32_t)j;
					p->blocks[j] = (uint8_t*)BGI_Calloc(size ? size : 1);
					if(p->sizes)
						p->sizes[j] = size ? size : 1;
					return ((p->firstTag + (slot >> (shift - p->bits))) << shift) |
						((slot & ((1u << (shift - p->bits)) - 1u)) << p->bits);
				}
		}
		return 0;
	}
	for(i = 0; i < gEngine->dynCount; i++)
		if(!gDynBlocks[i])
		{
			gDynBlocks[i] = (uint8_t*)BGI_Calloc(size ? size : 1);
			return ((uint32_t)i + gEngine->dynFirst) << VM_TAG_SHIFT;
		}
	return 0;
}

/* "80 21": free the dynamic block `ptr` points at.  1 on success, 0 when
 * ptr is not the start of an allocated block (the pooled layout of 1.535
 * on included). */
int GFree(uint32_t ptr)
{
	uint32_t slot;
	if(DYN_POOLED)
	{
		DynPool_t* p = DynPool_Of(ptr >> VM_TAG_SHIFT, ptr & VM_OFFSET_MASK, &slot);
		if(!p || !p->blocks || (ptr & ((1u << p->bits) - 1u)) || !p->blocks[slot]) // must point at the block start
			return 0;
		BGI_Free(p->blocks[slot]);
		p->blocks[slot] = NULL;
		return 1;
	}
	slot = (ptr >> VM_TAG_SHIFT) - gEngine->dynFirst;
	if(slot >= gEngine->dynCount)
		return 0;
	if(ptr & VM_OFFSET_MASK) // must point at the block start
		return 0;
	if(!gDynBlocks[slot])
		return 0;
	BGI_Free(gDynBlocks[slot]);
	gDynBlocks[slot] = NULL;
	return 1;
}

// the block of dynamic slot `slot` (tag - dynFirst; not for the pooled layout); NULL when free or out of range
uint8_t* DynMemBase(uint32_t slot)
{
	return slot < gEngine->dynCount ? gDynBlocks[slot] : NULL;
}

/* The host address of the tagged pointer `ptr` for thread t: NULL for 0,
 * else the global memory, the thread's code, data or heap area, or a
 * dynamic block.  A tag below the dynamic range that is none of those, or
 * a dynamic slot that is not allocated, is a script error (the message
 * names the pointer); no range check is made within an area. */
void* ResolvePtr(uint32_t ptr, Thread_t* t)
{
	uint32_t tag = ptr >> VM_TAG_SHIFT;
	uint32_t off = ptr & VM_OFFSET_MASK;
	char msg[0x100];
	uint8_t* base;

	if(ptr == 0)
		return NULL;
	if(tag == 0)
		return gGlobalMem + off;
	if(tag == gEngine->tagCode)
		return t->code + off;
	if(tag == gEngine->tagData)
		return t->data + off;
	if(tag == gEngine->tagHeap)
		return Thread_HeapPtr(t, off);
	if(tag < gEngine->dynFirst)
	{
		sprintf(msg, MSG_BAD_POINTER_REGION, (unsigned)ptr);
		ScriptError(msg, t);
	}
	if(DYN_POOLED)
	{
		uint32_t slot;
		DynPool_t* p = DynPool_Of(tag, off, &slot);
		base = p && p->blocks ? p->blocks[slot] : NULL;
		if(!base)
		{
			sprintf(msg, MSG_BAD_POINTER, (unsigned)ptr);
			ScriptError(msg, t);
		}
		return base + (off & ((1u << p->bits) - 1u)); // the offset within the slot
	}
	base = DynMemBase(tag - gEngine->dynFirst);
	if(!base)
	{
		sprintf(msg, MSG_BAD_POINTER, (unsigned)ptr);
		ScriptError(msg, t);
	}
	return base + off;
}

// pop a tagged pointer from the thread's stack and resolve it
void* PopPtr(Thread_t* t)
{
	return ResolvePtr(Thread_Pop(t), t);
}

/* A sized scalar access in 1.680 must fit its area or allocated pool slot. */
uint8_t* ResolveScalar680(uint32_t ptr, Thread_t* t, uint32_t width)
{
	uint32_t tag = ptr >> VM_TAG_SHIFT;
	uint32_t off = ptr & VM_OFFSET_MASK;
	uint32_t limit = 0;
	if(tag == 0)
		limit = gGlobalMemSize;
	else if(tag == gEngine->tagCode)
		limit = t->codeSize;
	else if(tag == gEngine->tagData)
		limit = t->dataSize;
	else if(tag == gEngine->tagHeap)
		limit = t->heap ? t->heap->size : 0;
	else
	{
		uint32_t slot;
		DynPool_t* pool = DynPool_Of(tag, off, &slot);
		if(pool && pool->blocks && pool->blocks[slot])
		{
			limit = pool->sizes[slot];
			off &= (1u << pool->bits) - 1u;
		}
	}
	if(!ptr || off >= limit || width > limit - off)
		ScriptError("Scalar reference lies outside its memory area", t);
	return (uint8_t*)ResolvePtr(ptr, t);
}

// resolve the n tagged pointers of in[] into out[] (the string lists of "80 38", "80 F2", "80 F4", "80 F5")
void ResolvePtrArray(void** out, Thread_t* t, const uint32_t* in, int n)
{
	int i;
	for(i = 0; i < n; i++)
		out[i] = ResolvePtr(in[i], t);
}
