#include "cmd_get_dma_addrs.h"

#include <stdio.h>

#include "tracelib/tracer_state_machine.h"

HRESULT HandleGetDMAAddrs(const char *command, char *response,
                          uint32_t response_len, CommandContext *ctx) {
  uint32_t push_addr, pull_addr;
  if (!TracerGetDMAAddresses(&push_addr, &pull_addr)) {
    snprintf(response, response_len, "invalid");
  } else {
    snprintf(response, response_len, "push=0x%X pull=0x%X", push_addr,
             pull_addr);
  }
  return XBOX_S_OK;
}
