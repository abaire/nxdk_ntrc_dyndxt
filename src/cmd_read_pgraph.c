#include "cmd_read_pgraph.h"

#include <string.h>

#include "tracelib/tracer_state_machine.h"
#include "xbdm_util.h"

static SendPrepopulatedBinaryDataContext send_context;
static uint8_t read_buffer[4096];

HRESULT HandleReadPGRAPH(const char *command, char *response,
                         uint32_t response_len, CommandContext *ctx) {
  TracerLockPGRAPHBuffer();
  uint32_t valid_bytes =
      TracerReadPGRAPHBuffer(read_buffer + 4, sizeof(read_buffer) - 4);
  TracerUnlockPGRAPHBuffer();
  if (!valid_bytes) {
    return XBOX_E_DATA_NOT_AVAILABLE;
  }

  memcpy(read_buffer, &valid_bytes, 4);

  InitializeSendPrepopulatedBinaryDataContexts(ctx, &send_context, read_buffer,
                                               valid_bytes + 4, FALSE);
  return XBOX_S_BINARY;
}
