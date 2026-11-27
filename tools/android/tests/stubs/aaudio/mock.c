/* SPDX-License-Identifier: GPL-2.0-only */
#include "AAudio.h"
#include "mock.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct AAudioStreamBuilder { int rate, channels, format; };
struct AAudioStream { int rate, channels, format; int64_t written; };
MockAudio_t mockAudio;

void MockAudioReset(void)
{
    assert(!mockAudio.builders && !mockAudio.streams);
    memset(&mockAudio, 0, sizeof mockAudio);
    mockAudio.nativeRate = 48000;
    mockAudio.channels = 2;
    mockAudio.format = AAUDIO_FORMAT_PCM_I16;
}

void OsAndroid_Log(const char* text) { assert(text && *text); ++mockAudio.logs; }
const char* AAudio_convertResultToText(aaudio_result_t code) { (void)code; return "mock device result"; }
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** b) { *b = calloc(1, sizeof **b); ++mockAudio.builders; return 0; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder* b) { free(b); --mockAudio.builders; return 0; }
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder* b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setUsage(AAudioStreamBuilder* b, int32_t v) { (void)b; (void)v; }
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder* b, int32_t v) { b->rate = v; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder* b, int32_t v) { b->channels = v; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder* b, int32_t v) { b->format = v; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b, AAudioStream** stream)
{
    ++mockAudio.opens;
    if((mockAudio.rejectRate && b->rate) || (mockAudio.rejectI16 && b->format == AAUDIO_FORMAT_PCM_I16))
        return -2;
    *stream = malloc(sizeof **stream);
    **stream = (AAudioStream){mockAudio.nativeRate, mockAudio.channels, mockAudio.format, 0};
    ++mockAudio.streams;
    return 0;
}
int32_t AAudioStream_getSampleRate(AAudioStream* stream) { return stream->rate; }
int32_t AAudioStream_getChannelCount(AAudioStream* stream) { return stream->channels; }
aaudio_format_t AAudioStream_getFormat(AAudioStream* stream) { return stream->format; }
int64_t AAudioStream_getFramesWritten(AAudioStream* stream) { return stream->written; }
int64_t AAudioStream_getFramesRead(AAudioStream* stream) { return stream->written; }
aaudio_result_t AAudioStream_requestStart(AAudioStream* stream)
{
    (void)stream;
    if(mockAudio.failStart) { --mockAudio.failStart; return -3; }
    return 0;
}
aaudio_result_t AAudioStream_close(AAudioStream* stream) { free(stream); --mockAudio.streams; return 0; }
aaudio_result_t AAudioStream_write(AAudioStream* stream, const void* data, int32_t count, int64_t timeout)
{
    int n = mockAudio.partial && count > mockAudio.partial ? mockAudio.partial : count;
    assert(timeout > 0);
    if(mockAudio.writeFailures) { --mockAudio.writeFailures; return -899; }
    if(mockAudio.writeError) return mockAudio.writeError;
    if(mockAudio.zeroWrite) return 0;
    if(mockAudio.writePauseNs) {
        struct timespec interval = {0, mockAudio.writePauseNs};
        nanosleep(&interval, NULL);
    }
    assert((size_t)mockAudio.capturedFrames + n <= sizeof mockAudio.captured / 4);
    for(int i = 0; i < n; i++) for(int c = 0; c < 2; c++) {
        int index = i * stream->channels + (stream->channels == 1 ? 0 : c);
        mockAudio.captured[2 * (mockAudio.capturedFrames + i) + c] = stream->format == AAUDIO_FORMAT_PCM_FLOAT ?
            (int16_t)(((const float*)data)[index] * 32768.0f) : ((const int16_t*)data)[index];
    }
    mockAudio.capturedFrames += n;
    stream->written += n;
    return n;
}
