#include "cmd_detach.h"

#include <stdio.h>

#include "tracer_state_machine.h"

HRESULT HandleDetach(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx) {
  TracerShutdown();
  snprintf(response, response_len, "Tracer shutdown requested");
  return XBOX_S_OK;
}
