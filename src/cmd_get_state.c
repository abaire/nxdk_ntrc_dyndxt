#include "cmd_get_state.h"

#include <stdio.h>

#include "tracer_state_machine.h"

HRESULT HandleGetState(const char *command, char *response, DWORD response_len,
                       CommandContext *ctx) {
  sprintf(response, "state=0x%X", TracerGetState());
  return XBOX_S_OK;
}
