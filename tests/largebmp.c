/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
/* Exercise synchronous and background bitmap loads with >32 MiB resources,
 * both from disk and from the decoded file cache. */
#include "bgi/file.h"
#include "bgi/codec.h"
#include "bgi/gfx/bmpops.h"
#include "bgi/gfx/bmpmgr.h"
#include "bgi/waitobj.h"
#include "bgi/wait.h"
#include "bgi/os.h"
#include "bgi/version.h"
#include "bgi/vm.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
static int failures;
#define CHECK(c)                                   \
	do                                             \
	{                                              \
		if(!(c))                                   \
		{                                          \
			printf("FAIL %d: %s\n", __LINE__, #c); \
			failures++;                            \
		}                                          \
	} while(0)

/* A small, generated v2 stream expands to the same geometry as the
 * 4140x2460 backgrounds involved in the Android crash. Each block has
 * zero DCT coefficients, so every output pixel is opaque mid-grey. */
static uint8_t* LargeCbg2(uint32_t* size)
{
	const uint32_t width=4140,height=2460,cols=(width+7)/8,rows=(height+7)/8;
	const uint32_t bandBytes=(cols+7)/8+1,base=192+(rows+1)*4;
	uint32_t streamBytes=base+rows*bandBytes+4,key=73;
	uint8_t* data=calloc(1,48+132+streamBytes);
	CbgHeader_t* header=(CbgHeader_t*)data;
	memcpy(header->magic,CBG_MAGIC,16);
	header->width=width;header->height=height;header->bpp=32;
	header->encLen=132;header->key=key;header->sum=128;header->version=2;
	for(uint32_t i=0;i<132;i++)data[48+i]=(uint8_t)((i<128?1:0)+(uint8_t)BGI_HashUpdate(&key));
	uint8_t* stream=data+48+132;
	stream[0]=stream[16]=1;
	for(uint32_t i=0;i<=rows;i++) {
		uint32_t offset=base+i*bandBytes;
		memcpy(stream+192+i*4,&offset,4);
		if(i<rows)memset(stream+offset,255,bandBytes-1);
	}
	*size=48+132+streamBytes;
	return data;
}

static void CompressedSync(Thread_t* thread)
{
	uint32_t stored;uint8_t* compressed=LargeCbg2(&stored);
	FILE* file=fopen("large.cbg","wb");CHECK(file!=NULL);
	if(file){CHECK(fwrite(compressed,1,stored,file)==stored);fclose(file);}
	free(compressed);
	CHECK(stored < 0x2000000u);
	CHECK(GetLoadFileSize(NULL,"large.cbg")==40737616u);
	Vm_TablesInit();
	strcpy((char*)thread->data, "large.cbg");
	Thread_Push(thread, 0);
	Thread_Push(thread, gEngine->tagData << gEngine->tagShift);
	CHECK(vm_optable_92[0x14](thread) == 0);
	Loader_FreeCache();BmpMgr_FreeAll(gBmpMgr);
	Loader_SetCacheSize(2u*40737616+128);
	for(int cached=0;cached<2;cached++) {
		if(cached)CHECK(unlink("large.cbg")==0);
		CHECK(BmpOp_LoadFile(0,NULL,"large.cbg")==0);
		Bmp_t bitmap;CHECK(BmpMgr_GetInfo(gBmpMgr,&bitmap,0));
		CHECK(bitmap.w==4140 && bitmap.h==2460);
		CHECK(bitmap.pixels[0]==128 && bitmap.pixels[3]==255);
		const uint8_t* last=bitmap.pixels+(size_t)(bitmap.h-1)*bitmap.pitch+(bitmap.w-1)*4;
		CHECK(last[0]==128 && last[1]==128 && last[2]==128 && last[3]==255);
		CHECK(BmpMgr_FreeSlot(gBmpMgr,0));
	}
	Loader_FreeCache();
}
int main(void)
{
	char dir[128];
	const int width = 2048, height = 4097;
	size_t bytes = 16 + (size_t)width * height * 4;
	uint8_t* raw = calloc(1, bytes);
	uint16_t header[8] = {2048, 4097, 32, 0, 0, 0, 0, 0};
	memcpy(raw, header, 16);
	raw[16] = 0x12;
	raw[bytes - 4] = 0x56;
	raw[bytes - 3] = 0x78;
	snprintf(dir, sizeof dir, "/tmp/bgi_large_%ld", (long)getpid());
	CHECK(mkdir(dir, 0700) == 0);
	CHECK(chdir(dir) == 0);
	OS_Init();
	Engine_ResetGames();
	CHECK(Engine_SelectProfile("1.659.8"));
	SetBaseDir(NULL);
	gArcMgr = FileSet_New();
	gBmpMgr = BmpMgr_New(2);
	FILE* f = fopen("large.raw", "wb");
	CHECK(f != NULL);
	if(f)
	{
		CHECK(fwrite(raw, 1, bytes, f) == bytes);
		fclose(f);
	}
	CHECK(GetLoadFileSize(NULL, "large.raw") == bytes);
	Thread_t* thread = Thread_New(64, 256, 256);
	/* The synchronous 90 10 branch used a fixed 32 MiB buffer even after
	 * its background counterpart was fixed. Test the real entry point. */
	CHECK(BmpOp_LoadFile(0, NULL, "large.raw") == 0);
	Bmp_t sync;
	CHECK(BmpMgr_GetInfo(gBmpMgr, &sync, 0));
	CHECK(sync.w == width && sync.h == height);
	CHECK(sync.pixels[(size_t)(height-1)*sync.pitch+(width-1)*4] == 0x56);
	CHECK(BmpMgr_FreeSlot(gBmpMgr, 0));
	for(int cached = 0; cached < 2; cached++)
	{
		if(cached)
		{
			Loader_SetCacheSize((uint32_t)bytes * 2 + 128);
			CHECK(Loader_CacheAdd(NULL, "large.raw", raw, (uint32_t)bytes) == 1);
			uint32_t size = 0;
			CHECK(Loader_CacheLookup(NULL, &size, NULL, "large.raw") == 1 && size == bytes);
			CHECK(unlink("large.raw") == 0); /* the second load must use the cache */
			CHECK(BmpOp_LoadFile(0, NULL, "large.raw") == 0);
			CHECK(BmpMgr_GetInfo(gBmpMgr, &sync, 0));
			CHECK(sync.pixels[(size_t)(height-1)*sync.pitch+(width-1)*4+1] == 0x78);
			CHECK(BmpMgr_FreeSlot(gBmpMgr, 0));
		}
		Wait_t* wait = WaitBmpLoad_New(thread, cached, "0", "large.raw");
		CHECK(wait->vt->poll(wait) == 1);
		Bmp_t bmp;
		CHECK(BmpMgr_GetInfo(gBmpMgr, &bmp, cached));
		CHECK(bmp.w == width && bmp.h == height);
		CHECK(bmp.pixels[0] == 0x12);
		CHECK(bmp.pixels[(size_t)(height - 1) * bmp.pitch + (width - 1) * 4] == 0x56);
		CHECK(bmp.pixels[(size_t)(height - 1) * bmp.pitch + (width - 1) * 4 + 1] == 0x78);
		wait->vt->destroy(wait);
	}
	CompressedSync(thread);
	Loader_FreeCache();
	BmpMgr_Delete(gBmpMgr);
	gBmpMgr = NULL;
	FileSet_Delete(gArcMgr);
	gArcMgr = NULL;
	Thread_Delete(thread);
	free(raw);
	OS_Shutdown();
	CHECK(chdir("/") == 0);
	CHECK(rmdir(dir) == 0);
	printf("Large bitmap synchronous/raw/CBG-v2/cache/background loads: %d failures\n", failures);
	return failures != 0;
}
