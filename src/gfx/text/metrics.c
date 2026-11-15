/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "bgi/gfx/text.h"
/* 92 94's records are keyed independently of the line count. Querying
 * with NULL keeps the record; copying removes it, matching the loader contract. */
typedef struct Metrics
{
	uint32_t token;
	int count;
	int32_t* heights;
	struct Metrics* next;
} Metrics;
static Metrics* records;
static uint32_t nextToken;
static int32_t* last;
static int lastCount;
void TextMetrics_Capture(const RichState_t* rich, int count, int height, int vertical)
{
	BGI_Free(last);
	last = NULL;
	lastCount = 0;
	if(count <= 0 || count > 65536)
		return;
	last = BGI_Alloc((size_t)count * sizeof *last);
	lastCount = count;
	for(int i = 0; i < count; i++)
		last[i] = 0;
	int line = -1, prev = 0;
	for(const RichChar_t* c = rich->head; c; c = c->next)
	{
		if(c->kind == 2)
			continue;
		int at = vertical ? c->x : c->y;
		if(line < 0 || at != prev)
		{
			line++;
			prev = at;
		}
		if(line >= count)
			break;
		if(c->emHeight > last[line])
			last[line] = c->emHeight;
	}
	for(int i = 0; i < count; i++)
		if(!last[i])
			last[i] = height;
}
uint32_t TextMetrics_Save(void)
{
	Metrics* m = BGI_Alloc(sizeof *m);
	if(++nextToken == 0)
		++nextToken;
	m->token = nextToken;
	m->count = lastCount;
	m->heights = BGI_Alloc((size_t)(lastCount ? lastCount : 1) * sizeof *last);
	if(lastCount)
		memcpy(m->heights, last, (size_t)lastCount * sizeof *last);
	m->next = records;
	records = m;
	return m->token;
}
int TextMetrics_Take(int32_t* out, uint32_t token)
{
	Metrics** link = &records;
	while(*link)
	{
		Metrics* m = *link;
		if(m->token == token)
		{
			int n = m->count;
			if(out)
			{
				memcpy(out, m->heights, (size_t)n * sizeof *out);
				*link = m->next;
				BGI_Free(m->heights);
				BGI_Free(m);
			}
			return n;
		}
		link = &m->next;
	}
	return 0;
}
void TextMetrics_Clear(void)
{
	while(records)
	{
		Metrics* m = records;
		records = m->next;
		BGI_Free(m->heights);
		BGI_Free(m);
	}
	BGI_Free(last);
	last = NULL;
	lastCount = 0;
}
