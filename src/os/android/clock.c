/* SPDX-License-Identifier: GPL-2.0-only */
#include "clock.h"
#include <pthread.h>
#include <time.h>

/* Clock reads can come from asset and audio threads as well as the VM. */
static pthread_mutex_t gClockLock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t gPausedNs, gPauseAt;
static int gClockPaused;

uint64_t OsAndroid_MonotonicNs(void)
{
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

void OsAndroid_ClockSetResumed(int resumed)
{
    pthread_mutex_lock(&gClockLock);
    if(!resumed && !gClockPaused) {
        uint64_t now = OsAndroid_MonotonicNs();
        if(now) { gPauseAt = now; gClockPaused = 1; }
    } else if(resumed && gClockPaused) {
        uint64_t now = OsAndroid_MonotonicNs();
        if(now >= gPauseAt) { gPausedNs += now - gPauseAt; gClockPaused = 0; }
    }
    pthread_mutex_unlock(&gClockLock);
}

uint64_t OsAndroid_ClockNs(void)
{
    pthread_mutex_lock(&gClockLock);
    uint64_t now = gClockPaused ? gPauseAt : OsAndroid_MonotonicNs();
    uint64_t result = now >= gPausedNs ? now - gPausedNs : 0;
    pthread_mutex_unlock(&gClockLock);
    return result;
}
