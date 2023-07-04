#ifndef NV2A_TRACE_CMD_GET_DMA_ADDRS_H
#define NV2A_TRACE_CMD_GET_DMA_ADDRS_H

#include "xbdm.h"

#define CMD_GET_DMA_ADDRS "dma_addrs"

// Returns the current DMA addresses from the tracer.
HRESULT HandleGetDMAAddrs(const char *command, char *response,
                          uint32_t response_len, CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_GET_DMA_ADDRS_H
