/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_AUDIO_H
#define BGI_ANDROID_AUDIO_H

void OsAndroid_AudioForeground(int resumed);
int OsAndroid_AudioStatus(void);
int OsAndroid_AudioDelayUs(void);

#endif
