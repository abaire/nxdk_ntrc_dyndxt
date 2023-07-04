#include "xbdm_util.h"

#include <string.h>

static inline uint32_t min(uint32_t a, uint32_t b) { return (a > b) ? a : b; }
static HRESULT_API SendStaticBufferBinaryData(CommandContext *ctx,
                                              char *response,
                                              DWORD response_len);

void InitializeSendPrepopulatedBinaryDataContexts(
    CommandContext *ctx, SendPrepopulatedBinaryDataContext *send_context,
    void *buffer, uint32_t buffer_size, BOOL free_on_complete) {
  send_context->buffer = buffer;
  send_context->read_offset = 0;
  send_context->free_buffer_on_complete = free_on_complete;

  ctx->buffer = buffer;
  ctx->user_data = send_context;
  ctx->buffer_size = buffer_size;
  ctx->handler = SendStaticBufferBinaryData;
  ctx->bytes_remaining = buffer_size;
}

static HRESULT_API SendStaticBufferBinaryData(CommandContext *ctx,
                                              char *response,
                                              DWORD response_len) {
  SendPrepopulatedBinaryDataContext *send_context =
      (SendPrepopulatedBinaryDataContext *)ctx->user_data;

  uint32_t bytes_to_send = min(ctx->buffer_size, ctx->bytes_remaining);
  if (!bytes_to_send) {
    if (send_context->free_buffer_on_complete) {
      DmFreePool(send_context->buffer);
    }
    return XBOX_S_NO_MORE_DATA;
  }

  memcpy(ctx->buffer, send_context->buffer + send_context->read_offset,
         bytes_to_send);
  ctx->data_size = bytes_to_send;
  send_context->read_offset += bytes_to_send;
  ctx->bytes_remaining -= bytes_to_send;

  return XBOX_S_OK;
}
