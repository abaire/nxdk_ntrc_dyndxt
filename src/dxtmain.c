#include "dxtmain.h"

#include <stdio.h>
#include <string.h>

#include "cmd_attach.h"
#include "cmd_detach.h"
#include "cmd_discard_until_flip.h"
#include "cmd_get_dma_addrs.h"
#include "cmd_get_state.h"
#include "cmd_hello.h"
#include "cmd_read_graphics.h"
#include "cmd_read_pgraph.h"
#include "cmd_trace_frame.h"
#include "cmd_wait_for_stable_push_buffer_state.h"
#include "nxdk_dxt_dll_main.h"
#include "tracelib/ntrc_dyndxt.h"
#include "tracelib/tracer_state_machine.h"

// Command prefix that will be handled by this processor.
// Keep in sync with value in ntrc.py
static const char kHandlerName[] = NTRC_HANDLER_NAME;
// static const uint32_t kTag = 0x6E747263;  // 'ntrc'

static const CommandTableEntry kCommandTableDef[] = {
    {CMD_ATTACH, HandleAttach},
    {CMD_DETACH, HandleDetach},
    {CMD_DISCARD_UNTIL_FLIP, HandleDiscardUntilFlip},
    {CMD_GET_DMA_ADDRS, HandleGetDMAAddrs},
    {CMD_GET_STATE, HandleGetState},
    {CMD_HELLO, HandleHello},
    {CMD_READ_GRAPHICS, HandleReadGraphics},
    {CMD_READ_PGRAPH, HandleReadPGRAPH},
    {CMD_TRACE_FRAME, HandleTraceFrame},
    {CMD_WAIT_FOR_STABLE_PUSH_BUFFER, HandleWaitForStablePushBufferState},
};
const CommandTableEntry *kCommandTable = kCommandTableDef;
const uint32_t kCommandTableNumEntries =
    sizeof(kCommandTableDef) / sizeof(kCommandTableDef[0]);

static HRESULT_API ProcessCommand(const char *command, char *response,
                                  DWORD response_len,
                                  struct CommandContext *ctx);
static void OnTracerStateChanged(TracerState new_state);
static void OnPGRAPHBufferBytesAvailable(uint32_t new_bytes);
static void OnGraphicsBufferBytesAvailable(uint32_t new_bytes);

HRESULT DXTMain(void) {
  TracerInitialize(OnTracerStateChanged, OnPGRAPHBufferBytesAvailable,
                   OnGraphicsBufferBytesAvailable);
  return DmRegisterCommandProcessor(kHandlerName, ProcessCommand);
}

static HRESULT_API ProcessCommand(const char *command, char *response,
                                  DWORD response_len,
                                  struct CommandContext *ctx) {
  const char *subcommand = command + sizeof(kHandlerName);

  const CommandTableEntry *entry = kCommandTable;
  for (uint32_t i = 0; i < kCommandTableNumEntries; ++i, ++entry) {
    uint32_t len = strlen(entry->command);
    if (!strncmp(subcommand, entry->command, len)) {
      return entry->processor(subcommand + len, response, response_len, ctx);
    }
  }

  return XBOX_E_UNKNOWN_COMMAND;
}

static void OnTracerStateChanged(TracerState new_state) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s!new_state=0x%X", kHandlerName, new_state);
  DmSendNotificationString(buf);
}

static void OnPGRAPHBufferBytesAvailable(uint32_t new_bytes) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s!w_pgraph=0x%X", kHandlerName, new_bytes);
  DmSendNotificationString(buf);
}

static void OnGraphicsBufferBytesAvailable(uint32_t new_bytes) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s!w_graphics=0x%X", kHandlerName, new_bytes);
  DmSendNotificationString(buf);
}
