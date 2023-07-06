#include "cmd_read_aux.h"

#include <string.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"
#include "xbdm_util.h"

static SendPrepopulatedBinaryDataContext send_context;
#define BUFFER_SIZE (1024 * 1024)
static uint8_t read_buffer[BUFFER_SIZE];

HRESULT HandleReadAux(const char *command, char *response,
                      uint32_t response_len, CommandContext *ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  uint32_t max_size = BUFFER_SIZE;
  if (CPGetUInt32("maxsize", &max_size, &cp)) {
    max_size = max_size > BUFFER_SIZE ? BUFFER_SIZE : max_size;
  }

  TracerLockAuxBuffer();
  uint32_t valid_bytes = TracerReadAuxBuffer(read_buffer + 4, max_size - 4);
  TracerUnlockAuxBuffer();
  if (!valid_bytes) {
    return XBOX_E_DATA_NOT_AVAILABLE;
  }

  memcpy(read_buffer, &valid_bytes, 4);

  InitializeSendPrepopulatedBinaryDataContexts(ctx, &send_context, read_buffer,
                                               valid_bytes + 4, FALSE);
  return XBOX_S_BINARY;
}
