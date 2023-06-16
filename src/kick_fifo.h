#ifndef NV2A_TRACE_KICK_FIFO_H
#define NV2A_TRACE_KICK_FIFO_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KickResult {
  KICK_OK,
  KICK_TIMEOUT,
  KICK_BAD_READ_PUSH_ADDR,
  KICK_PUSH_MODIFIED_IN_CALL,
} KickResult;

KickResult KickFIFO(DWORD expected_push);

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // NV2A_TRACE_KICK_FIFO_H
