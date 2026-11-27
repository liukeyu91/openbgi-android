/* SPDX-License-Identifier: GPL-2.0-only */
#include "requests.h"

int HostRequestsPump(HostRequests_t* pending, OpenBGI_t* engine, const HostMessageUi_t* ui)
{
    if(pending->positive) {
        int result = ui->result(ui->ui);
        if(!result) return 1;
        if(result != pending->positive && result != pending->negative)
            result = pending->negative ? pending->negative : pending->positive;
        OpenBGI_Answer(engine, pending->id, result);
        *pending = (HostRequests_t){0};
    }
    OpenBGI_Request_t request = {.size = sizeof request};
    while(OpenBGI_NextRequest(engine, &request)) {
        int positive = 0, negative = 0;
        if(request.kind == OPENBGI_ASK_MESSAGE) {
            switch(request.u.message.flags & 15) {
            case OPENBGI_MB_YESNO: positive = OPENBGI_IDYES; negative = OPENBGI_IDNO; break;
            case OPENBGI_MB_RETRYCANCEL: positive = OPENBGI_IDRETRY; negative = OPENBGI_IDCANCEL; break;
            case OPENBGI_MB_OKCANCEL: positive = OPENBGI_IDOK; negative = OPENBGI_IDCANCEL; break;
            default: positive = OPENBGI_IDOK; break;
            }
            if(ui->show(ui->ui, &request, positive, negative)) {
                *pending = (HostRequests_t){request.id, positive, negative};
                return 1;
            }
        }
        OpenBGI_Answer(engine, request.id, negative ? negative : positive);
    }
    return 0;
}
