#ifndef NV2A_TRACE_CMD_GET_STATE_H
#define NV2A_TRACE_CMD_GET_STATE_H

#include "xbdm.h"

#define CMD_GET_STATE "state"

// Returns the current tracer state.
HRESULT HandleGetState(const char *command, char *response, DWORD response_len,
                       CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_GET_STATE_H
