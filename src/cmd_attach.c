#include "cmd_attach.h"

#include <stdio.h>

#include "command_processor_util.h"
#include "tracer_state_machine.h"

HRESULT HandleAttach(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx) {
  TracerConfig config;
  TracerGetDefaultConfig(&config);

  CommandParameters cp;
  int32_t result = CPParseCommandParameters(command, &cp);
  if (result < 0) {
    return CPPrintError(result, response, response_len);
  }

  uint32_t val;
  if (CPGetUInt32("psize", &val, &cp)) {
    config.pgraph_circular_buffer_size = val;
  }

  if (CPGetUInt32("gsize", &val, &cp)) {
    config.graphics_circular_buffer_size = val;
  }

  if (CPGetUInt32("tcap", &val, &cp)) {
    config.texture_capture_enabled = val != 0;
  }

  if (CPGetUInt32("dcap", &val, &cp)) {
    config.surface_depth_capture_enabled = val != 0;
  }

  if (CPGetUInt32("ccap", &val, &cp)) {
    config.surface_color_capture_enabled = val != 0;
  }

  if (CPGetUInt32("rdicap", &val, &cp)) {
    config.rdi_capture_enabled = val != 0;
  }

  CPDelete(&cp);

  HRESULT ret = TracerCreate(&config);
  if (XBOX_SUCCESS(ret)) {
    snprintf(response, response_len, "Tracer created");
  } else {
    snprintf(response, response_len, "Tracer creation failed");
  }
  return ret;
}
