/* SPDX-License-Identifier: GPL-2.0-only */
#include "bgi/codec.h"

static unsigned calls;
uint32_t __real_BGI_CbgDecode(const void*, void*);
uint32_t __wrap_BGI_CbgDecode(const void* src, void* dst)
{
    ++calls;
    return __real_BGI_CbgDecode(src, dst);
}
#define CHECK(x) do { if(!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while(0)

static uint8_t* Image(int bpp, uint32_t key, uint32_t* size)
{
    uint8_t raw[16 + 32 * 24 * 4];
    RawImageHeader_t* h = (RawImageHeader_t*)raw;
    size_t n = 32 * 24 * (bpp / 8);
    memset(raw, 0, sizeof raw);
    h->width = 32; h->height = 24; h->bpp = (uint32_t)bpp;
    for(size_t i = 0; i < n; ++i) raw[16 + i] = (uint8_t)(i * 13 + i / 11);
    return BGI_CbgEncode(raw, key, size);
}

static void Depth(int bpp)
{
    uint32_t size;
    uint8_t *src = Image(bpp, 77, &size), expected[4096], got[4096];
    unsigned before;
    memset(expected, 0xc7, sizeof expected);
    CHECK(__real_BGI_CbgDecode(src, expected) == 0);
    BGI_CbgCacheSetLimit(65536);
    calls = 0;
    memset(got, 0xc7, sizeof got);
    CHECK(BGI_CbgDecodeCached(src, size, got) == 0);
    CHECK(calls == 1 && !memcmp(expected, got, sizeof got));
    memset(got, 0xc7, sizeof got); // caller mutations must not affect cached pixels
    CHECK(BGI_CbgDecodeCached(src, size, got) == 0);
    CHECK(calls == 1 && !memcmp(expected, got, sizeof got)); // includes unwritten 24-bit tail

    src[0x30] ^= 0x10; // changed bytes at the same address must be validated again
    CHECK(BGI_CbgDecodeCached(src, size, got) == 0x80000004u);
    CHECK(BGI_CbgDecodeCached(src, size, got) == 0x80000004u);
    CHECK(calls == 3); // failed decodes must never become cache hits
    src[0x30] ^= 0x10;
    CHECK(BGI_CbgDecodeCached(src, size, got) == 0 && calls == 3);

    // Input ownership stays with the caller; a copy at another address is a hit.
    uint8_t* copy = (uint8_t*)malloc(size);
    memcpy(copy, src, size); free(src);
    memset(got, 0xc7, sizeof got);
    CHECK(BGI_CbgDecodeCached(copy, size, got) == 0 && calls == 3);
    CHECK(!memcmp(expected, got, sizeof got));
    BGI_CbgCacheClear();
    CHECK(BGI_CbgDecodeCached(copy, size, got) == 0 && calls == 4);
    BGI_CbgCacheSetLimit(0);
    before = calls;
    CHECK(BGI_CbgDecodeCached(copy, size, got) == 0);
    CHECK(BGI_CbgDecodeCached(copy, size, got) == 0 && calls == before + 2);
    free(copy);
}

static void BudgetAndLru(void)
{
    uint32_t size[3]; uint8_t* src[3]; uint8_t dst[4096];
    for(int i = 0; i < 3; ++i) src[i] = Image(32, 100u + (unsigned)i, &size[i]);
    CHECK(size[0] == size[1] && size[1] == size[2]);
    // Enough for two entries, never three. No dependency on private entry layout.
    size_t bytes = size[0] + BGI_CbgOutSize(src[0]);
    BGI_CbgCacheSetLimit(bytes * 2 + 1024);
    calls = 0;
    CHECK(BGI_CbgDecodeCached(src[0], size[0], dst) == 0);
    CHECK(BGI_CbgDecodeCached(src[1], size[1], dst) == 0 && calls == 2);
    CHECK(BGI_CbgDecodeCached(src[0], size[0], dst) == 0 && calls == 2);
    CHECK(BGI_CbgDecodeCached(src[2], size[2], dst) == 0 && calls == 3);
    CHECK(BGI_CbgDecodeCached(src[0], size[0], dst) == 0 && calls == 3);
    CHECK(BGI_CbgDecodeCached(src[1], size[1], dst) == 0 && calls == 4);

    BGI_CbgCacheSetLimit(1); calls = 0; // oversized images decode without being retained
    CHECK(BGI_CbgDecodeCached(src[0], size[0], dst) == 0);
    CHECK(BGI_CbgDecodeCached(src[0], size[0], dst) == 0 && calls == 2);
    BGI_CbgCacheSetLimit(0);
    for(int i = 0; i < 3; ++i) free(src[i]);
}

int main(void)
{
    Depth(8); Depth(24); Depth(32); BudgetAndLru();
    puts("PASS CBG cache: exact pixels/tails, changed content, failed decodes, ownership, clear/disable, LRU and budget");
    return 0;
}
