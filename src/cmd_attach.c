#include "cmd_attach.h"

#include <stdio.h>

#include "command_processor_util.h"
#include "tracelib/tracer_state_machine.h"

HRESULT HandleAttach(const char *command, char *response, uint32_t response_len,
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
    config.aux_circular_buffer_size = val;
  }

  if (CPGetUInt32("tcap", &val, &cp)) {
    config.aux_tracing_config.texture_capture_enabled = val != 0;
  }

  if (CPGetUInt32("dcap", &val, &cp)) {
    config.aux_tracing_config.surface_depth_capture_enabled = val != 0;
  }

  if (CPGetUInt32("ccap", &val, &cp)) {
    config.aux_tracing_config.surface_color_capture_enabled = val != 0;
  }

  if (CPGetUInt32("rdicap", &val, &cp)) {
    config.aux_tracing_config.rdi_capture_enabled = val != 0;
  }

  if (CPGetUInt32("rawpgraph", &val, &cp)) {
    config.aux_tracing_config.raw_pgraph_capture_enabled = val != 0;
  }

  if (CPGetUInt32("rawpfb", &val, &cp)) {
    config.aux_tracing_config.raw_pfb_capture_enabled = val != 0;
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
