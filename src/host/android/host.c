/* SPDX-License-Identifier: GPL-2.0-only */
#include "host.h"
#include "files.h"
#include "fonts.h"
#include "../../os/android/clock.h"
#include "../../os/posix/posix_internal.h"

/* The host callback distinguishes UTF-8 from legacy text. GBK and CP950
 * are pinned by the launch option for this Android engine process. */
static int gLegacyTextEncoding=OS_TEXT_SJIS;

static int PerfNs(void* user, int64_t* out)
{
    (void)user;
    *out = (int64_t)OsAndroid_ClockNs();
    return *out != 0;
}

static uint32_t Ticks(void* user)
{
    (void)user;
    return (uint32_t)(OsAndroid_ClockNs() / 1000000);
}

static void LocalTime(void* user, OpenBGI_DateTime_t* out)
{
    (void)user;
    struct timespec t;
    struct tm local;
    *out = (OpenBGI_DateTime_t){0};
    if(clock_gettime(CLOCK_REALTIME, &t) != 0 || !localtime_r(&t.tv_sec, &local)) return;
    *out = (OpenBGI_DateTime_t){
        .year = local.tm_year + 1900, .month = local.tm_mon + 1, .dayOfWeek = local.tm_wday,
        .day = local.tm_mday, .hour = local.tm_hour, .minute = local.tm_min, .second = local.tm_sec,
        .millis = (uint16_t)(t.tv_nsec / 1000000),
    };
}

void AndroidHostTextEncoding(void* user, int utf8)
{
    (void)user;
    OsPosix_SetUtf8Paths(utf8);
    OS_SetTextEncoding(utf8 ? OS_TEXT_UTF8 : gLegacyTextEncoding);
}

OpenBGI_Host_t AndroidHostConfig(void)
{
    return (OpenBGI_Host_t){.size = sizeof(OpenBGI_Host_t), .api = OPENBGI_API_VERSION,
        .ticksMs = Ticks, .perfNs = PerfNs, .localTime = LocalTime,
        .files = &AndroidHostFiles, .fontRenderer = &AndroidHostFonts,
        .textEncoding = AndroidHostTextEncoding};
}

const char* AndroidHostGameOptions(const AndroidGameSettings_t* settings, char* args, size_t size)
{
    /* The core reads Shift-JIS unless told otherwise.  A translated game's text
     * is UTF-8, GBK or Big5, and its boot script does not say so: pin the mode
     * with --text-encoding=, which "81 00" then cannot switch back. */
    char encoding[24]="";
    int legacy=OS_TEXT_SJIS;
    if(strcmp(settings->encoding,"sjis")) {
        if(!strcmp(settings->encoding,"gbk")) legacy=OS_TEXT_GBK;
        else if(!strcmp(settings->encoding,"big5")) legacy=OS_TEXT_BIG5;
        else if(strcmp(settings->encoding,"utf8"))
            return "This game's text encoding is unknown to the engine.";
        snprintf(encoding,sizeof encoding,"--text-encoding=%s ",settings->encoding);
    }
    const char* profile=settings->profile;
    if(!strcmp(profile,"auto"))
        profile="";
    else {
        int i=0;
        const char* supported;
        while((supported=OpenBGI_ProfileName(i++)) && strcmp(supported,profile)) {}
        if(!supported) return "The selected engine profile is unavailable in this core.";
    }
    int length=snprintf(args,size,"%s%s%s%s. \"Do not use mutex.\"",
        *profile ? "--engine=" : "",profile,*profile ? " " : "",encoding);
    if(length<0 || (size_t)length>=size) return "Could not prepare the engine arguments.";
    if(!OsPosix_SetArchiveLanguage(settings->archiveLanguage))
        return "The selected archive language is unavailable.";
    gLegacyTextEncoding=legacy;
    AndroidHostTextEncoding(NULL,0);
    return NULL;
}
