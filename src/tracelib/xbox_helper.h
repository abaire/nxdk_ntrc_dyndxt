#ifndef NV2A_TRACE_XBOX_HELPER_H
#define NV2A_TRACE_XBOX_HELPER_H

#include <windows.h>

#include "tracelib/exchange_dword.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DMAState {
  BOOL non_increasing;
  uint32_t method;
  uint32_t subchannel;
  uint32_t method_count;
  uint32_t error;
} DMAState;

//! Returns a uint32_t value from the given address.
uint32_t ReadDWORD(intptr_t address);

//! Writes the given uint32_t value to the given address.
void WriteDWORD(intptr_t address, uint32_t value);

void DisablePGRAPHFIFO(void);
void EnablePGRAPHFIFO(void);
void BusyWaitUntilPGRAPHIdle(void);

void PauseFIFOPuller(void);
void ResumeFIFOPuller(void);

void PauseFIFOPusher(void);
void ResumeFIFOPusher(void);
void BusyWaitUntilPusherIDLE(void);

//! Attempt to populate the FIFO cache by briefly unpausing the pusher.
//! The pusher will be left in a paused state on exit.
void MaybePopulateFIFOCache(uint32_t sleep_milliseconds);

uint32_t GetDMAPushAddress(void);
uint32_t GetDMAPullAddress(void);
void SetDMAPushAddress(uint32_t target);

void GetDMAState(DMAState *result);

// Returns the PGRAPH graphics class registered for the given subchannel.
uint32_t FetchGraphicsClassForSubchannel(uint32_t subchannel);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NV2A_TRACE_XBOX_HELPER_H
