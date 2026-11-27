/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BGI_ANDROID_HOST_REQUESTS_H
#define BGI_ANDROID_HOST_REQUESTS_H

#include "openbgi.h"

typedef struct HostRequests {
    uint32_t id;
    int positive, negative;
} HostRequests_t;

typedef struct HostMessageUi {
    int (*show)(void* ui, const OpenBGI_Request_t* request, int positive, int negative);
    int (*result)(void* ui); /* 0 while open, -1 if the platform can no longer show it */
    void* ui;
} HostMessageUi_t;

/* Called on the engine thread. Returns 1 while a message awaits a choice. */
int HostRequestsPump(HostRequests_t* pending, OpenBGI_t* engine, const HostMessageUi_t* ui);

#endif
