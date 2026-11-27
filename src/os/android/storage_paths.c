/* SPDX-License-Identifier: GPL-2.0-only */
#include "storage_paths.h"
#include "../posix/posix_internal.h"

int OsAndroid_ReadActiveGame(const char* internal, char* id, size_t size)
{
    char path[PATH_MAX];
    if(!internal || !*internal || size < 2 ||
       snprintf(path,sizeof path,"%s/active-game.txt",internal)>=(int)sizeof path) return -1;
    FILE* file=fopen(path,"r");
    if(!file) return errno==ENOENT ? 0 : -1;
    int valid=fgets(id,(int)size,file)!=NULL && fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if(!valid) return -1;
    id[strcspn(id,"\r\n")]=0;
    return *id && strspn(id,"abcdefghijklmnopqrstuvwxyz0123456789-")==strlen(id) ? 1 : -1;
}

int OsAndroid_InitStorage(const char* internal, const char* external, void (*log)(const char*))
{
    char path[PATH_MAX], id[80], selected[PATH_MAX], image[PATH_MAX]="";
    int linked=0, mounted=0;
    FILE* file;
    if(OsAndroid_ReadActiveGame(internal,id,sizeof id)!=1) return 0;
    snprintf(selected,sizeof selected,"%s/games/%s",internal,id);
    snprintf(gPosixConfigDir,sizeof gPosixConfigDir,"%s/.openbgi",selected);
    if(mkdir(gPosixConfigDir,0700)!=0 && errno!=EEXIST)return 0;
    snprintf(gPosixRegFile,sizeof gPosixRegFile,"%s/registry.txt",gPosixConfigDir);
    if(strcmp(id,"external")==0) {
        if(!external || !*external)return 0;
        snprintf(selected,sizeof selected,"%s/game",external);
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
            if(log)log(note);
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
    if(log)log(selected);
    return 1;
}
