#ifndef NV2A_TRACE_TRACER_STATE_MACHINE_H
#define NV2A_TRACE_TRACER_STATE_MACHINE_H

#include <windows.h>

#include "ntrc_dyndxt.h"

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
