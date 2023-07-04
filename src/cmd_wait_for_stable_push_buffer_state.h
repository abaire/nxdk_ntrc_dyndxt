#ifndef NV2A_TRACE_CMD_WAIT_FOR_STABLE_PUSH_BUFFER_STATE_H
#define NV2A_TRACE_CMD_WAIT_FOR_STABLE_PUSH_BUFFER_STATE_H

#include "xbdm.h"

#define CMD_WAIT_FOR_STABLE_PUSH_BUFFER "wait_stable_pb"

// Puts the state machine into a wait loop until the push buffer is stable.
HRESULT HandleWaitForStablePushBufferState(const char *command, char *response,
                                           uint32_t response_len,
                                           CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_WAIT_FOR_STABLE_PUSH_BUFFER_STATE_H
