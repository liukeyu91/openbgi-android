/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OPENBGI_TEST_AAUDIO_H
#define OPENBGI_TEST_AAUDIO_H
#include <stdint.h>
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_format_t;
typedef struct AAudioStreamBuilder AAudioStreamBuilder;
typedef struct AAudioStream AAudioStream;
enum {AAUDIO_OK=0,AAUDIO_UNSPECIFIED=0,AAUDIO_DIRECTION_OUTPUT=0,AAUDIO_USAGE_GAME=14,AAUDIO_FORMAT_PCM_I16=1,AAUDIO_FORMAT_PCM_FLOAT=2};
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder**);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder*);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*,int32_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*,aaudio_format_t);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder*,AAudioStream**);
int32_t AAudioStream_getSampleRate(AAudioStream*);
int32_t AAudioStream_getChannelCount(AAudioStream*);
aaudio_format_t AAudioStream_getFormat(AAudioStream*);
int64_t AAudioStream_getFramesWritten(AAudioStream*);
int64_t AAudioStream_getFramesRead(AAudioStream*);
aaudio_result_t AAudioStream_requestStart(AAudioStream*);
aaudio_result_t AAudioStream_close(AAudioStream*);
aaudio_result_t AAudioStream_write(AAudioStream*,const void*,int32_t,int64_t);
const char* AAudio_convertResultToText(aaudio_result_t);
#endif
