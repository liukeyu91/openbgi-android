/*
 * This file is part of OpenBGI (https://openbgi.net).
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "android_internal.h"
#include "../posix/posix_internal.h"

static int GamePresent(const char* dir)
{
	char path[PATH_MAX];
	struct stat st;
	snprintf(path, sizeof path, "%s/system.arc", dir);
	if(stat(path, &st) == 0 && S_ISREG(st.st_mode))
		return 1;
	snprintf(path, sizeof path, "%s/ipl._bp", dir);
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int OsAndroid_InitPaths(void)
{
	AndroidHost_t* host = gAndroidHost;
	char internal[PATH_MAX], external[PATH_MAX];
	const char* selected;
	int len;
	if(!host || !host->internal[0])
		return 0;
	len = snprintf(gPosixConfigDir, sizeof gPosixConfigDir, "%s/openbgi", host->internal);
	if(len < 0 || len >= (int)sizeof gPosixConfigDir)
		return 0;
	if(mkdir(gPosixConfigDir, 0700) != 0 && errno != EEXIST)
		return 0;
	len = snprintf(gPosixRegFile, sizeof gPosixRegFile, "%s/registry.txt", gPosixConfigDir);
	if(len < 0 || len >= (int)sizeof gPosixRegFile)
		return 0;
	len = snprintf(internal, sizeof internal, "%s/game", host->internal);
	if(len < 0 || len >= (int)sizeof internal)
		return 0;
	selected = internal;
	if(host->external[0])
	{
		len = snprintf(external, sizeof external, "%s/game", host->external);
		if(len > 0 && len < (int)sizeof external)
		{
			mkdir(external, 0700);
			if(!GamePresent(internal) && GamePresent(external))
				selected = external;
		}
	}
	if(mkdir(selected, 0700) != 0 && errno != EEXIST)
		return 0;
	if(!OS_SetGameDir(selected))
		return 0;
	OsAndroid_Log(selected);
	return 1;
}
