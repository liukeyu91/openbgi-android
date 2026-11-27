/* SPDX-License-Identifier: GPL-2.0-only */
#include "android_internal.h"
#include "storage_paths.h"
#include "storage_settings.h"
#include "bgi/version.h"
#include "../posix/posix_internal.h"

int OsAndroid_InitPaths(void)
{
    AndroidHost_t* host=gAndroidHost;
    return host && OsAndroid_InitStorage(host->internal,host->external,OsAndroid_Log);
}

void OsAndroid_EngineOptions(char* out,size_t size)
{
    AndroidGameSettings_t settings;
    OsAndroid_ReadGameSettings(&settings);
    const char* profile=settings.profile;
    if(strcmp(profile,"auto")){
        int i,known=0;
        for(i=0;i<Engine_ProfileCount();i++)if(!strcmp(Engine_ProfileAt(i)->name,profile))known=1;
        if(!known)profile="auto";
    }
    int explicitProfile=strcmp(profile,"auto")!=0;
    snprintf(out,size,"--text-encoding=%s%s%s%s%s",settings.encoding,
        explicitProfile?" --engine=":"",explicitProfile?profile:"",
        *settings.archiveLanguage?" --archive-language=":"",settings.archiveLanguage);
    OsAndroid_Log(out);
}
