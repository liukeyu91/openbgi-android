/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_AUDIO_OUTPUT_H
#define BGI_ANDROID_AUDIO_OUTPUT_H
#include <aaudio/AAudio.h>
#include <stdint.h>
/* All fields belong to the mixer thread. Phase carries across 20 ms blocks. */
typedef struct AndroidAudioOutput {
    AAudioStream* stream;
    int sourceRate, rate, channels, capacity;
    aaudio_format_t format;
    uint64_t phase;
    int havePrevious;
    int16_t previous[2];
    int16_t* pcm;
    void* converted;
} AndroidAudioOutput_t;
int AndroidAudio_Open(AndroidAudioOutput_t* out,int sourceRate,int period,int verbose);
int AndroidAudio_Write(AndroidAudioOutput_t* out,const int16_t* stereo,int frames);
void AndroidAudio_Close(AndroidAudioOutput_t* out);
#endif
