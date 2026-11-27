/* SPDX-License-Identifier: GPL-2.0-only */
#include "storage_settings.h"
#include "../posix/posix_internal.h"

static int IsScriptEncoding(const char* encoding)
{
    return !strcmp(encoding,"sjis") || !strcmp(encoding,"gbk") ||
           !strcmp(encoding,"big5") || !strcmp(encoding,"utf8");
}

void OsAndroid_ReadGameSettings(AndroidGameSettings_t* settings)
{
    *settings = (AndroidGameSettings_t){.encoding = "sjis", .profile = "auto"};
    char path[PATH_MAX], line[256];
    snprintf(path,sizeof path,"%s/encoding.txt",gPosixConfigDir);
    FILE* file=fopen(path,"r");
    if(file) {
        if(fgets(line,sizeof line,file)) {
            line[strcspn(line,"\r\n")]=0;
            if(IsScriptEncoding(line)) strcpy(settings->encoding,line);
        }
        fclose(file);
    }
    snprintf(path,sizeof path,"%s/settings.ini",gPosixConfigDir);
    file=fopen(path,"r");
    if(!file) return;
    while(fgets(line,sizeof line,file)) {
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"encoding=",9) && IsScriptEncoding(line+9))
            strcpy(settings->encoding,line+9);
        if(!strncmp(line,"archiveLanguage=",16) &&
            (!strcmp(line+16,"cn") || !strcmp(line+16,"tc") || !strcmp(line+16,"en")))
            strcpy(settings->archiveLanguage,line+16);
        if(!strncmp(line,"profile=",8)) {
            const char* name=line+8;
            size_t len=strlen(name);
            strcpy(settings->profile,len>0 && len<sizeof settings->profile &&
                strspn(name,"0123456789./")==len ? name : "auto");
        }
        if(!strncmp(line,"scale=",6) && line[6]>='0' && line[6]<='2' && !line[7])
            settings->scale=line[6]-'0';
    }
    fclose(file);
}
