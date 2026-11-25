/* SPDX-License-Identifier: GPL-2.0-only */
#include "android_internal.h"
#include "bgi/version.h"
#include "../posix/posix_internal.h"

int OsAndroid_InitPaths(void)
{
    AndroidHost_t* host=gAndroidHost;
    char path[PATH_MAX], id[80], selected[PATH_MAX], image[PATH_MAX];
    int linked=0, mounted=0;
    FILE* file;
    size_t i;
    if(!host || !host->internal[0]) return 0;
    image[0]=0;
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
    } else {
        snprintf(path,sizeof path,"%s/source-path.txt",gPosixConfigDir);
        file=fopen(path,"r");
        if(file) {
            if(!fgets(selected,sizeof selected,file) || fgetc(file)!=EOF ||
                selected[0]!='/' || strpbrk(selected,"\r\n")) {fclose(file);return 0;}
            fclose(file);
            linked=1;
        } else if(errno!=ENOENT)return 0;
        if(!linked) {
            /* A container entry keeps its resources in a disc image or a ZIP
             * archive; the mount is what the file services read through
             * (paths.c: OsPosix_DiscPath). The entry directory itself only
             * holds the settings and the private data layer. source-image.txt
             * is the name earlier builds wrote. */
            snprintf(path,sizeof path,"%s/source.txt",gPosixConfigDir);
            file=fopen(path,"r");
            if(!file && errno==ENOENT) {
                snprintf(path,sizeof path,"%s/source-image.txt",gPosixConfigDir);
                file=fopen(path,"r");
            }
            if(file) {
                if(!fgets(image,sizeof image,file) || fgetc(file)!=EOF ||
                    image[0]!='/' || strpbrk(image,"\r\n")) {fclose(file);return 0;}
                fclose(file);
            } else if(errno!=ENOENT)return 0;
        }
    }
    if(!OS_SetGameDir(selected))return 0;
    if(image[0]) {
        if(!OsPosix_MountDisc(image)) {
            char note[PATH_MAX+32];
            snprintf(note,sizeof note,"cannot mount disc image: %s",image);
            OsAndroid_Log(note);
            return 0;
        }
        mounted=1;
    }
    if(linked || mounted) {
        if(snprintf(path,sizeof path,"%s/data",gPosixConfigDir)>=(int)sizeof path ||
            !OsPosix_SetDataDirectory(path))return 0;
    } else OsPosix_SetDataDirectory("");
    OsPosix_SetPatchDirectory("");
    snprintf(path,sizeof path,"%s/patches/active",gPosixConfigDir);
    file=fopen(path,"r");
    if(file) {
        int valid=fgets(id,sizeof id,file)!=NULL && fgetc(file)==EOF && strlen(id)==36 &&
            strspn(id,"0123456789abcdef-")==36;
        fclose(file);
        if(!valid || snprintf(path,sizeof path,"%s/patches/%s",gPosixConfigDir,id)>=(int)sizeof path ||
            !OsPosix_SetPatchDirectory(path))return 0;
    } else if(errno!=ENOENT)return 0;
    OsAndroid_Log(selected);
    return 1;
}

static int IsScriptEncoding(const char* encoding)
{
    return !strcmp(encoding,"sjis") || !strcmp(encoding,"gbk") ||
           !strcmp(encoding,"big5") || !strcmp(encoding,"utf8");
}

void OsAndroid_EngineOptions(char* out,size_t size)
{
    char path[PATH_MAX], encoding[16]="sjis", profile[32]="auto", line[256];
    char language[3]="";
    FILE* file;
    snprintf(path,sizeof path,"%s/encoding.txt",gPosixConfigDir);
    file=fopen(path,"r");
    if(file) { if(!fgets(encoding,sizeof encoding,file))strcpy(encoding,"sjis"); fclose(file); }
    encoding[strcspn(encoding,"\r\n")]=0;
    if(!IsScriptEncoding(encoding))strcpy(encoding,"sjis");
    snprintf(path,sizeof path,"%s/settings.ini",gPosixConfigDir);
    file=fopen(path,"r");
    if(file){
        while(fgets(line,sizeof line,file)){
            line[strcspn(line,"\r\n")]=0;
            if(!strncmp(line,"encoding=",9) && IsScriptEncoding(line+9))snprintf(encoding,sizeof encoding,"%s",line+9);
            if(!strncmp(line,"archiveLanguage=",16) &&
                (!strcmp(line+16,"cn") || !strcmp(line+16,"tc") || !strcmp(line+16,"en")))
                memcpy(language,line+16,sizeof language);
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
    int explicitProfile=strcmp(profile,"auto")!=0;
    snprintf(out,size,"--text-encoding=%s%s%s%s%s",encoding,
        explicitProfile?" --engine=":"",explicitProfile?profile:"",
        *language?" --archive-language=":"",language);
    OsAndroid_Log(out);
}
