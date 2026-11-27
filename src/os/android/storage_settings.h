/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_STORAGE_SETTINGS_H
#define BGI_ANDROID_STORAGE_SETTINGS_H

typedef struct {
    char encoding[16], profile[32], archiveLanguage[3];
    int scale;
} AndroidGameSettings_t;

/* Read the settings of the entry selected by OsAndroid_InitStorage. */
void OsAndroid_ReadGameSettings(AndroidGameSettings_t* settings);

#endif
