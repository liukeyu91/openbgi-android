/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
/* Exercise the actual background bitmap wait with a generated >32 MiB
 * resource, both from disk and from the decoded file cache. */
#include "bgi/file.h"
#include "bgi/gfx/bmpmgr.h"
#include "bgi/waitobj.h"
#include "bgi/wait.h"
#include "bgi/os.h"
#include "bgi/version.h"
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
	CHECK(GetFileSize_(NULL, "large.raw") == bytes);
	Thread_t* thread = Thread_New(64, 256, 256);
	for(int cached = 0; cached < 2; cached++)
	{
		if(cached)
		{
			Loader_SetCacheSize((uint32_t)bytes * 2 + 128);
			CHECK(Loader_CacheAdd(NULL, "large.raw", raw, (uint32_t)bytes) == 1);
			uint32_t size = 0;
			CHECK(Loader_CacheLookup(NULL, &size, NULL, "large.raw") == 1 && size == bytes);
			CHECK(unlink("large.raw") == 0); /* the second load must use the cache */
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
	printf("Large bitmap wait: %d failures\n", failures);
	return failures != 0;
}
