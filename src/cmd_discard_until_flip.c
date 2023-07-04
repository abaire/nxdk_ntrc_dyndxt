#include "cmd_discard_until_flip.h"

#include <stdio.h>
#include <string.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"

HRESULT HandleDiscardUntilFlip(const char *command, char *response,
                               uint32_t response_len, CommandContext *ctx) {
  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  BOOL require_flip = CPHasKey("require_flip", &cp);
  HRESULT ret = TracerBeginDiscardUntilFlip(require_flip);

  *response = 0;
  if (XBOX_SUCCESS(ret)) {
    strncat(response, "Waiting until next framebuffer flip...", response_len);
  } else {
    snprintf(response, response_len, "Failed: %X", ret);
  }

  return ret;
}
