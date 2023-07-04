#ifndef NV2A_TRACE_EXCHANGE_U32_H
#define NV2A_TRACE_EXCHANGE_U32_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes the given uint32_t value to the given address, returning the previous
// value.
uint32_t ExchangeDWORD(intptr_t address, uint32_t value);

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // NV2A_TRACE_EXCHANGE_U32_H
