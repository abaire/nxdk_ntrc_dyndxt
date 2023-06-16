#ifndef NV2A_TRACE_CMD_HELLO_H
#define NV2A_TRACE_CMD_HELLO_H

#include "xbdm.h"

#define CMD_HELLO "hello"

// Enumerates the command table.
HRESULT HandleHello(const char *command, char *response, DWORD response_len,
                    CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_HELLO_H
