#ifndef NTRC_DYNDXT_SRC_CMD_TRACE_FRAME_H_
#define NTRC_DYNDXT_SRC_CMD_TRACE_FRAME_H_

#include "xbdm.h"

#define CMD_TRACE_FRAME "trace_frame"

// Traces a single frame. Must be in a stable state, generally at the beginning
// of a frame (via HandleWaitForStablePushBufferState and HandleDiscardUntilFlip
// respectively).
HRESULT HandleTraceFrame(const char *command, char *response,
                         DWORD response_len, CommandContext *ctx);

#endif  // NTRC_DYNDXT_SRC_CMD_TRACE_FRAME_H_
