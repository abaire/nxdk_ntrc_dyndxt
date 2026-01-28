#include "cmd_read_aux.h"

#include <string.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"
#include "xbdm_util.h"

#define BUFFER_SIZE (1024 * 1024)

HRESULT HandleReadAux(const char* command, char* response,
                      uint32_t response_len, CommandContext* ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  uint32_t max_size = BUFFER_SIZE;
  if (CPGetUInt32("maxsize", &max_size, &cp)) {
    max_size = max_size > BUFFER_SIZE ? BUFFER_SIZE : max_size;
  }

  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(max_size, 'taxb');
  if (!buffer) {
    return XBOX_E_FAIL;
  }

  TracerLockAuxBuffer();
  uint32_t valid_bytes = TracerReadAuxBuffer(buffer + 4, max_size - 4);
  TracerUnlockAuxBuffer();
  if (!valid_bytes) {
    DmFreePool(buffer);
    return XBOX_E_DATA_NOT_AVAILABLE;
  }

  memcpy(buffer, &valid_bytes, 4);

  SendPrepopulatedBinaryDataContext* send_context =
      (SendPrepopulatedBinaryDataContext*)DmAllocatePoolWithTag(
          sizeof(SendPrepopulatedBinaryDataContext), 'taxc');
  if (!send_context) {
    DmFreePool(buffer);
    return XBOX_E_FAIL;
  }

  InitializeSendPrepopulatedBinaryDataContexts(ctx, send_context, buffer,
                                               valid_bytes + 4, TRUE, TRUE);
  return XBOX_S_BINARY;
}
