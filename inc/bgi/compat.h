/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_COMPAT_H_
#define BGI_COMPAT_H_
#include "bgi/common.h"

#define COMPAT_MAX_ISSUES 64
#define COMPAT_MAX_PROGRAM (8u * 1024u * 1024u)
typedef struct CompatIssue {
	uint32_t opcode, offset;
	int kind; /* 1: unimplemented; 2: undefined in profile; 3: unavailable on host */
} CompatIssue_t;
typedef struct CompatResult {
	uint32_t instructions, problems, total;
	int count;
	CompatIssue_t issues[COMPAT_MAX_ISSUES];
} CompatResult_t;

/* Read-only configuration in a process which does not run the VM. The caller
 * serializes a configure/check session. No engine initialization or execution. */
const char* Compat_Configure(const char* gamesJson, const char* profile, const uint8_t* boot, uint32_t size);
int Compat_CheckProgram(const uint8_t* file, uint32_t size, CompatResult_t* result);
#endif
