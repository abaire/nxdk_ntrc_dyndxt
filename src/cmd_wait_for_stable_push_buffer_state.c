#include "cmd_wait_for_stable_push_buffer_state.h"

#include <stdio.h>
#include <string.h>

#include "tracer_state_machine.h"
#include "xbdm.h"

HRESULT HandleWaitForStablePushBufferState(const char *command, char *response,
                                           DWORD response_len,
                                           CommandContext *ctx) {
  HRESULT ret = TracerBeginWaitForStablePushBufferState();

  *response = 0;
  if (XBOX_SUCCESS(ret)) {
    strncat(response, "Waiting for stable pushbuffer state...", response_len);
  } else {
    snprintf(response, response_len, "Failed: %X", ret);
  }

  return ret;
}
