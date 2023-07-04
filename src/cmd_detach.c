#include "cmd_detach.h"

#include <stdio.h>

#include "tracelib/tracer_state_machine.h"

HRESULT HandleDetach(const char *command, char *response, uint32_t response_len,
                     CommandContext *ctx) {
  TracerShutdown();
  snprintf(response, response_len, "Tracer shutdown requested");
  return XBOX_S_OK;
}
