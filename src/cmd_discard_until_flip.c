#include "cmd_discard_until_flip.h"

#include <stdio.h>
#include <string.h>

#include "tracer_state_machine.h"

HRESULT HandleDiscardUntilFlip(const char *command, char *response,
                               DWORD response_len, CommandContext *ctx) {
  HRESULT ret = TracerBeginDiscardUntilFlip();

  *response = 0;
  if (XBOX_SUCCESS(ret)) {
    strncat(response, "Waiting until next framebuffer flip...", response_len);
  } else {
    sprintf(response, "Failed: %X", ret);
  }

  return ret;
}
