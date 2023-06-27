#ifndef NV2A_TRACE_EXCHANGE_U32_H
#define NV2A_TRACE_EXCHANGE_U32_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes the given DWORD value to the given address, returning the previous
// value.
DWORD ExchangeDWORD(intptr_t address, DWORD value);

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // NV2A_TRACE_EXCHANGE_U32_H
