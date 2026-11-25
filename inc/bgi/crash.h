/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_CRASH_H
#define BGI_CRASH_H
#include <stdint.h>
#include <stddef.h>

/* Android fatal-signal diagnostics. No game text or save data is recorded. */
void OsAndroid_CrashInstall(const char* logDirectory);
void OsAndroid_CrashThread(void);
void OsAndroid_CrashInstruction(uint32_t thread, uint32_t ip, uint32_t relative,
    uint32_t opcode, const char* module);
int OsAndroid_LastInstruction(char* program, size_t size, uint32_t* relative, uint32_t* opcode);
#endif
