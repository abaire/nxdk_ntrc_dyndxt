#include "cmd_trace_frame.h"

#include <stdio.h>
#include <string.h>

#include "tracelib/tracer_state_machine.h"

HRESULT HandleTraceFrame(const char *command, char *response,
                         DWORD response_len, CommandContext *ctx) {
  HRESULT ret = TracerTraceCurrentFrame();

  if (XBOX_SUCCESS(ret)) {
    strncpy(response, "Tracing current frame...", response_len);
  } else {
    snprintf(response, response_len, "Failed: 0x%X", ret);
  }

  return ret;
}
