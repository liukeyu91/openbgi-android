/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_CLOCK_H
#define BGI_ANDROID_CLOCK_H

#include <stdint.h>

uint64_t OsAndroid_MonotonicNs(void);
uint64_t OsAndroid_ClockNs(void);
void OsAndroid_ClockSetResumed(int resumed);

#endif
