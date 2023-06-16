#include "cmd_detach.h"

#include <stdio.h>

#include "tracer_state_machine.h"

HRESULT HandleDetach(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx) {
  TracerDestroy();
  sprintf(response, "Tracer stopped and destroyed");
  return XBOX_S_OK;
}
