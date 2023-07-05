#include "cmd_read_aux.h"

#include <string.h>

#include "tracelib/tracer_state_machine.h"
#include "xbdm_util.h"

static SendPrepopulatedBinaryDataContext send_context;
static uint8_t read_buffer[4096];

HRESULT HandleReadAux(const char *command, char *response,
                      uint32_t response_len, CommandContext *ctx) {
  TracerLockAuxBuffer();
  uint32_t valid_bytes =
      TracerReadAuxBuffer(read_buffer + 4, sizeof(read_buffer) - 4);
  TracerUnlockAuxBuffer();
  if (!valid_bytes) {
    return XBOX_E_DATA_NOT_AVAILABLE;
  }

  memcpy(read_buffer, &valid_bytes, 4);

  InitializeSendPrepopulatedBinaryDataContexts(ctx, &send_context, read_buffer,
                                               valid_bytes + 4, FALSE);
  return XBOX_S_BINARY;
}
