#ifndef NV2A_TRACE_TRACER_STATE_MACHINE_H
#define NV2A_TRACE_TRACER_STATE_MACHINE_H

#include <windows.h>

#include "pgraph_command_callbacks.h"
#include "tracelib/ntrc_dyndxt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TracerConfig {
  // Number of bytes to reserve for pgraph command capture.
  uint32_t pgraph_circular_buffer_size;

  // Number of bytes to reserve for color/depth buffer/etc... capture.
  uint32_t aux_circular_buffer_size;

  AuxConfig aux_tracing_config;
} TracerConfig;

// Callback to be invoked when the tracer state changes.
typedef void (*NotifyStateChangedHandler)(TracerState);

// Callback to be invoked when a request has been completed.
typedef void (*NotifyRequestProcessedHandler)(void);

// Callback to be invoked when bytes are written to a circular buffer.
typedef void (*NotifyBytesAvailableHandler)(uint32_t bytes_written);

//! Initializes the tracer library.
//! The given function will be called anytime the tracer state machine changes
//! state.
HRESULT TracerInitialize(
    NotifyStateChangedHandler on_notify_state_changed,
    NotifyRequestProcessedHandler on_notify_request_processed,
    NotifyBytesAvailableHandler on_pgraph_buffer_bytes_available,
    NotifyBytesAvailableHandler on_aux_buffer_bytes_available);

//! Populates the given TracerConfig with default values.
void TracerGetDefaultConfig(TracerConfig *config);

//! Creates a tracer instance with the given config.
HRESULT TracerCreate(const TracerConfig *config);

//! Requests that the tracer shutdown.
void TracerShutdown(void);

TracerState TracerGetState(void);

//! Fetches the last saved DMA addresses. Returns TRUE if they are valid, else
//! FALSE.
BOOL TracerGetDMAAddresses(uint32_t *push_addr, uint32_t *pull_addr);

//! True if a request is actively being processed.
BOOL TracerIsProcessingRequest(void);
HRESULT TracerBeginWaitForStablePushBufferState(void);
HRESULT TracerBeginDiscardUntilFlip(BOOL require_new_frame);
HRESULT TracerTraceCurrentFrame(void);

//! Locks the PGRAPH buffer to prevent writing, returning the bytes available in
//! the buffer.
uint32_t TracerLockPGRAPHBuffer(void);
//! Copies up to `size` bytes from the PGRAPH buffer into `buffer`, returning
//! the number of bytes actually copied.
uint32_t TracerReadPGRAPHBuffer(void *buffer, uint32_t size);
//! Releases the lock on the PGRAPH buffer.
void TracerUnlockPGRAPHBuffer(void);

//! Locks the Graphics buffer to prevent writing, returning the bytes available
//! in the buffer.
uint32_t TracerLockAuxBuffer(void);
//! Copies up to `size` bytes from the Graphics buffer into `buffer`, returning
//! the number of bytes actually copied.
uint32_t TracerReadAuxBuffer(void *buffer, uint32_t size);
//! Releases the lock on the Graphics buffer.
void TracerUnlockAuxBuffer(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NV2A_TRACE_TRACER_STATE_MACHINE_H
