/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_LEB128_H
#define BGI_LEB128_H

#include <stdint.h>

/* Read a signed base-128 immediate. Zero means truncated or outside int64_t. */
static inline uint32_t ReadSignedLeb128(const uint8_t* bytes, uint32_t size, int64_t* value)
{
	uint64_t bits = 0;
	uint32_t i;
	for(i = 0; i < size && i < 10; i++)
	{
		uint8_t b = bytes[i];
		unsigned shift = i * 7;
		if(i == 9 && b != 0 && b != 0x7f)
			return 0;
		bits |= (uint64_t)(b & 0x7f) << shift;
		if(!(b & 0x80))
		{
			if((b & 0x40) && i < 9)
				bits |= UINT64_MAX << (shift + 7);
			*value = bits <= INT64_MAX ? (int64_t)bits : -1 - (int64_t)~bits;
			return i + 1;
		}
	}
	return 0;
}

static inline uint32_t ReadSignedLeb32(const uint8_t* bytes, uint32_t size, int64_t* value)
{
	int64_t decoded;
	uint32_t n = ReadSignedLeb128(bytes, size < 5 ? size : 5, &decoded);
	if(!n)
		return 0;
	/* 1.680 accumulates into a 32-bit register, so the fifth byte only
	 * contributes its low four bits. */
	uint32_t bits = (uint32_t)decoded;
	*value = bits <= INT32_MAX ? (int64_t)bits : (int64_t)bits - INT64_C(0x100000000);
	return n;
}

/* Fixed-width payloads used by 1.680's push instruction. */
static inline uint32_t PushImmediateWidth680(uint8_t mode)
{
	return mode >= 0x80 && mode <= 0x83 ? 1u << (mode & 3) : 0;
}

#endif
