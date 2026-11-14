/* SPDX-License-Identifier: GPL-2.0-only */
#include "android_internal.h"
#include "bgi/version.h"
#include "../posix/posix_internal.h"

int OsAndroid_InitPaths(void)
{
    AndroidHost_t* host=gAndroidHost;
    char path[PATH_MAX], id[80], selected[PATH_MAX];
    FILE* file;
    size_t i;
    if(!host || !host->internal[0]) return 0;
    snprintf(path,sizeof path,"%s/active-game.txt",host->internal);
    file=fopen(path,"r");
    if(!file) return 0;
    if(!fgets(id,sizeof id,file)) { fclose(file); return 0; }
    fclose(file);
    id[strcspn(id,"\r\n")]=0;
    if(!id[0])return 0;
    for(i=0;id[i];++i) if(!((id[i]>='a'&&id[i]<='z') || (id[i]>='0'&&id[i]<='9') || id[i]=='-'))return 0;
    snprintf(selected,sizeof selected,"%s/games/%s",host->internal,id);
    snprintf(gPosixConfigDir,sizeof gPosixConfigDir,"%s/.openbgi",selected);
    if(mkdir(gPosixConfigDir,0700)!=0 && errno!=EEXIST)return 0;
    snprintf(gPosixRegFile,sizeof gPosixRegFile,"%s/registry.txt",gPosixConfigDir);
    if(strcmp(id,"external")==0) {
        if(!host->external[0])return 0;
        snprintf(selected,sizeof selected,"%s/game",host->external);
    }
    if(!OS_SetGameDir(selected))return 0;
    OsAndroid_Log(selected);
    return 1;
}

void OsAndroid_EngineOptions(char* out,size_t size)
{
    char path[PATH_MAX], encoding[16]="sjis", profile[32]="auto", line[256];
    FILE* file;
    snprintf(path,sizeof path,"%s/encoding.txt",gPosixConfigDir);
    file=fopen(path,"r");
    if(file) { if(!fgets(encoding,sizeof encoding,file))strcpy(encoding,"sjis"); fclose(file); }
    encoding[strcspn(encoding,"\r\n")]=0;
    if(strcmp(encoding,"sjis") && strcmp(encoding,"gbk") && strcmp(encoding,"utf8"))strcpy(encoding,"sjis");
    snprintf(path,sizeof path,"%s/settings.ini",gPosixConfigDir);
    file=fopen(path,"r");
    if(file){
        while(fgets(line,sizeof line,file)){
            line[strcspn(line,"\r\n")]=0;
            if(!strncmp(line,"encoding=",9) && (!strcmp(line+9,"sjis") || !strcmp(line+9,"gbk") || !strcmp(line+9,"utf8")))snprintf(encoding,sizeof encoding,"%s",line+9);
            if(!strncmp(line,"profile=",8)){
                size_t i,len=strlen(line+8);int valid=len>0 && len<sizeof profile;
                for(i=8;line[i];++i)if(!((line[i]>='0' && line[i]<='9') || line[i]=='.' || line[i]=='/'))valid=0;
                if(valid)snprintf(profile,sizeof profile,"%s",line+8);
                else strcpy(profile,"auto");
            }
        }
        fclose(file);
    }
    if(strcmp(profile,"auto")){
        int i,known=0;
        for(i=0;i<Engine_ProfileCount();i++)if(!strcmp(Engine_ProfileAt(i)->name,profile))known=1;
        if(!known)strcpy(profile,"auto");
    }
    if(strcmp(profile,"auto"))snprintf(out,size,"--text-encoding=%s --engine=%s",encoding,profile);
    else snprintf(out,size,"--text-encoding=%s",encoding);
    OsAndroid_Log(out);
}
