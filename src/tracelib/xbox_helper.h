#ifndef NV2A_TRACE_XBOX_HELPER_H
#define NV2A_TRACE_XBOX_HELPER_H

#include <stdbool.h>
#include <windows.h>

#include "register_defs.h"
#include "tracelib/exchange_dword.h"
#include "xemu/hw/xbox/nv2a/nv2a_regs.h"

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
//! Spin waits until the CACHE1 status indicates the cache is empty.
void BusyWaitUntilCACHE1Empty(void);
void BusyWaitUntilPGRAPHIdle(void);
bool BusyWaitUntilPGRAPHIdleWithTimeout(uint32_t timeout_milliseconds);

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

void GetDMAState(DMAState* result);

// Returns the PGRAPH graphics class registered for the given subchannel.
uint32_t FetchGraphicsClassForSubchannel(uint32_t subchannel);

static inline bool CACHE1Empty() {
  return ReadDWORD(CACHE1_STATUS) & NV_PFIFO_CACHE1_STATUS_LOW_MARK;
}

static inline bool CACHE1Full() {
  return ReadDWORD(CACHE1_STATUS) & NV_PFIFO_CACHE1_STATUS_HIGH_MARK;
}

//! Indicates whether the PFIFO engine considers the DMA buffer to be fully
//! consumed at the instant of the call.
static inline bool DMAPushBufferEmpty() {
  uint32_t state = ReadDWORD(CACHE1_DMA_PUSH);
  return state & NV_PFIFO_CACHE1_DMA_PUSH_BUFFER;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NV2A_TRACE_XBOX_HELPER_H
