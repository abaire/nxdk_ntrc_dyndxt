#ifndef NV2A_TRACE_XBOX_HELPER_H
#define NV2A_TRACE_XBOX_HELPER_H

#include <windows.h>

#include "tracelib/exchange_dword.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DMAState {
  BOOL non_increasing;
  DWORD method;
  DWORD subchannel;
  DWORD method_count;
  DWORD error;
} DMAState;

// Returns a DWORD value from the given address.
DWORD ReadDWORD(intptr_t address);

// Writes the given DWORD value to the given address.
void WriteDWORD(intptr_t address, DWORD value);

void DisablePGRAPHFIFO(void);
void EnablePGRAPHFIFO(void);
void BusyWaitUntilPGRAPHIdle(void);

void PauseFIFOPuller(void);
void ResumeFIFOPuller(void);

void PauseFIFOPusher(void);
void ResumeFIFOPusher(void);
void BusyWaitUntilPusherIDLE(void);

// Attempt to populate the FIFO cache by briefly unpausing the pusher.
// The pusher will be left in a paused state on exit.
void MaybePopulateFIFOCache(void);

DWORD GetDMAPushAddress(void);
DWORD GetDMAPullAddress(void);
void SetDMAPushAddress(DWORD target);

void GetDMAState(DMAState *result);

// Returns the current PGRAPH graphics class.
DWORD FetchActiveGraphicsClass(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NV2A_TRACE_XBOX_HELPER_H
