/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_HOST_AUDIO_H
#define BGI_ANDROID_HOST_AUDIO_H

#include "openbgi.h"

typedef struct HostAudio {
    OpenBGI_t* engine;
    int foreground;
    int running;
    int reading;
} HostAudio_t;

/* The engine thread calls this between frames, and with foreground=0 before Stop. */
void HostAudioUpdate(HostAudio_t* audio, OpenBGI_t* engine, int foreground);

#endif
