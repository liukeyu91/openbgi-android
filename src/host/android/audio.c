/* SPDX-License-Identifier: GPL-2.0-only */
#include "audio.h"
#include "../../os/android/audio.h"
#include "bgi/os.h"

static void Pull(void* user, int16_t* samples, int frames)
{
    HostAudio_t* audio = user;
    if(OsAndroid_AudioStatus() > 0 && __atomic_load_n(&audio->reading, __ATOMIC_ACQUIRE))
        OpenBGI_AudioRead(audio->engine, samples, frames);
}

void HostAudioUpdate(HostAudio_t* audio, OpenBGI_t* engine, int foreground)
{
    if(foreground != audio->foreground)
    {
        audio->foreground = foreground;
        OsAndroid_AudioForeground(foreground);
        if(foreground)
        {
            audio->engine = engine;
            audio->running = OS_AudioStart(OPENBGI_AUDIO_RATE, Pull, audio);
        }
        else if(audio->running)
        {
            __atomic_store_n(&audio->reading, 0, __ATOMIC_RELEASE);
            OS_AudioStop();
            audio->running = 0;
            OpenBGI_SetAudioLead(engine, 0);
        }
    }
    if(!audio->running)
        return;

    int ready = OsAndroid_AudioStatus() > 0;
    if(ready == __atomic_load_n(&audio->reading, __ATOMIC_RELAXED))
        return;
    if(ready)
    {
        /* One 20 ms read plus time for the engine to present its next frame. */
        OpenBGI_SetAudioLead(engine, 60);
        OpenBGI_RunAudio(engine);
    }
    __atomic_store_n(&audio->reading, ready, __ATOMIC_RELEASE);
    if(!ready)
        OpenBGI_SetAudioLead(engine, 0);
}
