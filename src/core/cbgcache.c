/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Repeated layered portraits often request the same decoded images through
 * Loader_QueueFile, bypassing the script-controlled bitmap file cache. Keep a
 * small LRU of successful CBG results. Comparing all input bytes makes loose
 * file replacements, archive reloads and different games safe without relying
 * on names, timestamps or a collision-prone content hash.
 */
#include "bgi/codec.h"

#define CBG_CACHE_MAX (64u * 1024u * 1024u)
#define CBG_CACHE_ENTRIES 16

typedef struct CbgCacheEntry
{
    struct CbgCacheEntry* next;
    size_t cost;
    uint32_t sourceSize, decodedSize;
    uint8_t data[]; // compressed source, followed by decoder-written bytes
} CbgCacheEntry_t;

static CbgCacheEntry_t* gEntries;
static size_t gLimit, gUsed;
static unsigned gCount;

static void EvictOldest(void)
{
    CbgCacheEntry_t **p = &gEntries, *entry;
    while((*p)->next) p = &(*p)->next;
    entry = *p;
    *p = NULL;
    gUsed -= entry->cost;
    --gCount;
    free(entry);
}

void BGI_CbgCacheClear(void)
{
    while(gEntries) EvictOldest();
}

void BGI_CbgCacheSetLimit(size_t bytes)
{
    BGI_CbgCacheClear();
    gLimit = bytes > CBG_CACHE_MAX ? CBG_CACHE_MAX : bytes;
}

uint32_t BGI_CbgDecodeCached(const void* src, uint32_t srcSize, void* dst)
{
    const CbgHeader_t* h = (const CbgHeader_t*)src;
    CbgCacheEntry_t **p, *entry;
    uint64_t decoded, cost;
    uint32_t bpp, result;

    if(!gLimit || srcSize < sizeof *h || !BGI_IsCbg(src))
        return BGI_CbgDecode(src, dst);
    bpp = h->bpp & 0xffffu;
    if(bpp != 8 && bpp != 24 && bpp != 32)
        return BGI_CbgDecode(src, dst);
    decoded = 16u + (uint64_t)h->width * h->height * (bpp >> 3);
    cost = sizeof(CbgCacheEntry_t) + (uint64_t)srcSize + decoded;
    if(cost > gLimit)
        return BGI_CbgDecode(src, dst);

    for(p = &gEntries; (entry = *p) != NULL; p = &entry->next)
        if(entry->sourceSize == srcSize && !memcmp(entry->data, src, srcSize))
        {
            *p = entry->next;
            entry->next = gEntries;
            gEntries = entry;
            memcpy(dst, entry->data + srcSize, entry->decodedSize);
            return 0;
        }

    result = BGI_CbgDecode(src, dst);
    if(result != 0) return result; // failures are never cached

    while(gEntries && (gUsed + (size_t)cost > gLimit || gCount >= CBG_CACHE_ENTRIES))
        EvictOldest();
    // The cache is optional: allocation failure must not fail an image load.
    entry = (CbgCacheEntry_t*)malloc((size_t)cost);
    if(!entry) return result;
    entry->cost = (size_t)cost;
    entry->sourceSize = srcSize;
    entry->decodedSize = (uint32_t)decoded;
    memcpy(entry->data, src, srcSize);
    memcpy(entry->data + srcSize, dst, (size_t)decoded);
    entry->next = gEntries;
    gEntries = entry;
    gUsed += entry->cost;
    ++gCount;
    return result;
}
