/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OPENBGI_TEST_AAUDIO_MOCK_H
#define OPENBGI_TEST_AAUDIO_MOCK_H

#include <stdint.h>

/* For worker tests, configure before starting and inspect after joining. */
typedef struct MockAudio {
    int nativeRate, channels, format;
    int rejectRate, rejectI16, failStart, writeError, zeroWrite, partial, writeFailures;
    int opens, builders, streams, logs;
    int capturedFrames;
    long writePauseNs;
    int16_t captured[800000];
} MockAudio_t;

extern MockAudio_t mockAudio;
void MockAudioReset(void);

#endif
