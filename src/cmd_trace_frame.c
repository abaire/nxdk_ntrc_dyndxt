#include "cmd_trace_frame.h"

#include <stdio.h>
#include <string.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"

HRESULT HandleTraceFrame(const char* command, char* response,
                         uint32_t response_len, CommandContext* ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  HRESULT ret = TracerTraceCurrentFrame(CPHasKey("nodiscard", &cp));

  if (XBOX_SUCCESS(ret)) {
    strncpy(response, "Tracing current frame...", response_len);
  } else {
    snprintf(response, response_len, "Failed: 0x%X", ret);
  }

  return ret;
}
