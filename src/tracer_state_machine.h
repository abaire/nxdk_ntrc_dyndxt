#ifndef NV2A_TRACE_TRACER_STATE_MACHINE_H
#define NV2A_TRACE_TRACER_STATE_MACHINE_H

#include <windows.h>

// Note: Entries with explicit values are intended for consumption by Python.
typedef enum TracerState {
  STATE_FATAL_ERROR_DISCARDING_FAILED = -1010,
  STATE_FATAL_ERROR_PROCESS_PUSH_BUFFER_COMMAND_FAILED = -1000,

  STATE_SHUTDOWN_REQUESTED = -2,
  STATE_SHUTDOWN = -1,

  STATE_UNINITIALIZED = 0,

  STATE_INITIALIZING = 1,
  STATE_INITIALIZED = 2,

  STATE_IDLE = 100,
  STATE_IDLE_STABLE_PUSH_BUFFER = 101,
  STATE_IDLE_NEW_FRAME = 102,
  STATE_IDLE_LAST,  // Last entry in the block of "idle" states.

  STATE_WAITING_FOR_STABLE_PUSH_BUFFER = 1000,

  STATE_DISCARDING_UNTIL_FLIP = 1010,
} TracerState;

typedef struct TracerConfig {
  // Number of bytes to reserve for pgraph command capture.
  DWORD pgraph_circular_buffer_size;

  // Number of bytes to reserve for color/depth buffer capture.
  DWORD graphics_circular_buffer_size;

  // Enables capture of RDI state into the graphics circular buffer.
  BOOL rdi_capture_enabled;

  // Enables capture of color surface into the graphics circular buffer.
  BOOL surface_color_capture_enabled;

  // Enables capture of depth surface into the graphics circular buffer.
  BOOL surface_depth_capture_enabled;

  // Enables capture of texture stage sources into the graphics circular buffer.
  BOOL texture_capture_enabled;
} TracerConfig;

// Callback to be invoked when the tracer state changes.
typedef void (*NotifyStateChangedHandler)(TracerState);

HRESULT TracerInitialize(NotifyStateChangedHandler on_notify_state_changed);

void TracerGetDefaultConfig(TracerConfig *config);

HRESULT TracerCreate(const TracerConfig *config);
void TracerDestroy(void);

TracerState TracerGetState(void);
BOOL TracerGetDMAAddresses(DWORD *push_addr, DWORD *pull_addr);

HRESULT TracerBeginWaitForStablePushBufferState(void);
HRESULT TracerBeginDiscardUntilFlip(void);

#endif  // NV2A_TRACE_TRACER_STATE_MACHINE_H
