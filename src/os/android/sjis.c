/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Android's iconv lacks code page 932. Keep the same CP932 path and
 * engine-message interface as the POSIX backend using a portable table.
 */
#include "bgi/os.h"
#include "bgi/os_common.h"
#include "bgi/os_posix.h"

#include "cp932.inc"

uint32_t OS_SjisToCodePoint(uint16_t code)
{
	unsigned lead = code >> 8, trail = code & 0xff;
	unsigned row, col;
	uint16_t cp;
	if(!OsCommon_SjisIsLead((uint8_t)lead) || trail < 0x40 || trail > 0xfc || trail == 0x7f)
		return 0xfffd;
	col = trail - 0x40 - (trail > 0x7f);
	if(lead >= 0xf0 && lead <= 0xf9)
		return 0xe000 + (lead - 0xf0) * 188 + col;
	row = lead <= 0x9f ? lead - 0x81 : lead - 0xe0 + 31;
	cp = kCp932[row * 188 + col];
	return cp ? cp : 0xfffd;
}

static uint16_t ToSjis(uint32_t cp)
{
	size_t lo = 0, hi = BGI_COUNTOF(kCp932ByUnicode);
	if(cp < 0x80)
		return (uint16_t)cp;
	if(cp >= 0xff61 && cp <= 0xff9f)
		return (uint16_t)(0xa1 + cp - 0xff61);
	if(cp >= 0xe000 && cp <= 0xe757)
	{
		unsigned i = cp - 0xe000, trail = i % 188 + 0x40;
		return (uint16_t)(((0xf0 + i / 188) << 8) | (trail + (trail >= 0x7f)));
	}
	while(lo < hi)
	{
		size_t mid = lo + (hi - lo) / 2;
		if(OS_SjisToCodePoint(kCp932ByUnicode[mid]) < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	if(lo < BGI_COUNTOF(kCp932ByUnicode) && OS_SjisToCodePoint(kCp932ByUnicode[lo]) == cp)
		return kCp932ByUnicode[lo];
	return 0;
}

uint32_t OsPosix_SjisChar(uint16_t code)
{
	return OS_SjisToCodePoint(code);
}

int OsPosix_UnicodeToSjis(uint32_t cp, char out[2])
{
	uint16_t code = ToSjis(cp);
	if(code > 0xff)
	{
		out[0] = (char)(code >> 8);
		out[1] = (char)code;
		return 2;
	}
	out[0] = (char)code;
	return code || !cp ? 1 : 0;
}

size_t OS_SjisToUtf8(const char* s, char* out, size_t n)
{
	return OsCommon_SjisToUtf8(s, out, n, OS_SjisToCodePoint);
}

void OsPosix_SjisToUtf8(const char* s, char* out, size_t n)
{
	OS_SjisToUtf8(s, out, n);
}

size_t OS_Utf8ToSjis(const char* utf8, char* out, size_t n)
{
	const uint8_t* p = (const uint8_t*)utf8;
	size_t pos = 0;
	if(!n)
		return 0;
	while(*p)
	{
		char code[2];
		int len = 1, count;
		uint32_t cp = OsCommon_Utf8SeqLen(p) ? OsCommon_Utf8Decode(p, &len) : *p;
		count = OsPosix_UnicodeToSjis(cp, code);
		if(!count)
		{
			code[0] = '?';
			count = 1;
		}
		if((size_t)count >= n - pos)
			break;
		memcpy(out + pos, code, (size_t)count);
		pos += (size_t)count;
		p += len;
	}
	out[pos] = 0;
	return pos;
}
