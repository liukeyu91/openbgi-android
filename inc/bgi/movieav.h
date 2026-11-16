/* This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_MOVIEAV_H
#define BGI_MOVIEAV_H
#include <stdint.h>
#include <stddef.h>

typedef struct MovieAv MovieAv_t;
typedef struct MovieAvInfo
{
    int width, height, audio;
    int32_t durationMs;
} MovieAvInfo_t;

/* Local files only. Open probes synchronously with an interrupt deadline.
 * Start launches demux/video/audio workers with bounded queues. */
MovieAv_t* MovieAv_Open(const char* path,int audio,MovieAvInfo_t* info,char* error,size_t size);
int MovieAv_Start(MovieAv_t* movie,int width,int height);
int MovieAv_Ready(MovieAv_t* movie);
void MovieAv_Begin(MovieAv_t* movie);
/* The caller owns a width*height*4 BGRA buffer. Returns 1 for a due frame. */
int MovieAv_Frame(MovieAv_t* movie,int64_t clockUs,uint8_t* pixels);
/* A mixer supplies source-rate frames; audio consumption is the master clock.
 * With no mixer, wallUs is used and stale audio is discarded. */
int64_t MovieAv_Clock(MovieAv_t* movie,int64_t wallUs,int audioMaster);
void MovieAv_Mix(MovieAv_t* movie,int32_t* stereo,int frames,int gain16);
int MovieAv_Done(MovieAv_t* movie,int64_t clockUs);
int MovieAv_Error(MovieAv_t* movie,char* error,size_t size);
void MovieAv_Close(MovieAv_t* movie);
#endif
