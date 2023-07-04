#ifndef NV2A_TRACE_CMD_DETACH_H
#define NV2A_TRACE_CMD_DETACH_H

#include "xbdm.h"

#define CMD_DETACH "detach"

// Exits the tracer and attempts to restore the xbox to a normal running state.
HRESULT HandleDetach(const char *command, char *response, uint32_t response_len,
                     CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_DETACH_H
