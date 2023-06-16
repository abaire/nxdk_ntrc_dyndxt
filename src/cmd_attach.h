#ifndef NV2A_TRACE_CMD_ATTACH_H
#define NV2A_TRACE_CMD_ATTACH_H

#include "xbdm.h"

#define CMD_ATTACH "attach"

// Creates a new tracer instance.
HRESULT HandleAttach(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_ATTACH_H
