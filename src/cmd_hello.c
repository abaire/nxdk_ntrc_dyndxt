#include "cmd_hello.h"

#include <string.h>

#include "dxtmain.h"

static HRESULT_API SendHelloData(CommandContext *ctx, char *response,
                                 DWORD response_len);

HRESULT HandleHello(const char *command, char *response, DWORD response_len,
                    CommandContext *ctx) {
  ctx->user_data = 0;
  ctx->handler = SendHelloData;
  *response = 0;
  strncat(response, "Available commands:", response_len);
  return XBOX_S_MULTILINE;
}

static HRESULT_API SendHelloData(CommandContext *ctx, char *response,
                                 DWORD response_len) {
  uint32_t current_index = (uint32_t)ctx->user_data++;

  if (current_index >= kCommandTableNumEntries) {
    return XBOX_S_NO_MORE_DATA;
  }

  const CommandTableEntry *entry = &kCommandTable[current_index];
  uint32_t command_len = strlen(entry->command) + 1;
  if (command_len > ctx->buffer_size) {
    response[0] = 0;
    strncat(response, "Response buffer is too small", response_len);
    return XBOX_E_ACCESS_DENIED;
  }

  memcpy(ctx->buffer, entry->command, command_len);
  return XBOX_S_OK;
}