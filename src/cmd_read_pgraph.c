#include "cmd_read_pgraph.h"

#include <string.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"
#include "xbdm_util.h"

#define READ_BUFFER_SIZE (1024 * 128 + 4)

HRESULT HandleReadPGRAPH(const char* command, char* response,
                         uint32_t response_len, CommandContext* ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  uint32_t max_size = READ_BUFFER_SIZE;
  if (CPGetUInt32("maxsize", &max_size, &cp)) {
    max_size = max_size > READ_BUFFER_SIZE ? READ_BUFFER_SIZE : max_size;
  }

  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(max_size, 'tpgb');
  if (!buffer) {
    return XBOX_E_FAIL;
  }

  TracerLockPGRAPHBuffer();
  uint32_t valid_bytes = TracerReadPGRAPHBuffer(buffer + 4, max_size - 4);
  TracerUnlockPGRAPHBuffer();
  if (!valid_bytes) {
    DmFreePool(buffer);
    return XBOX_E_DATA_NOT_AVAILABLE;
  }

  memcpy(buffer, &valid_bytes, 4);

  SendPrepopulatedBinaryDataContext* send_context =
      (SendPrepopulatedBinaryDataContext*)DmAllocatePoolWithTag(
          sizeof(SendPrepopulatedBinaryDataContext), 'tpgc');
  if (!send_context) {
    DmFreePool(buffer);
    return XBOX_E_FAIL;
  }

  InitializeSendPrepopulatedBinaryDataContexts(ctx, send_context, buffer,
                                               valid_bytes + 4, TRUE, TRUE);
  return XBOX_S_BINARY;
}
