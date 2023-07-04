#include "tracer_state_machine.h"

#include "pushbuffer_command.h"
#include "register_defs.h"
#include "tracelib/exchange_dword.h"
#include "tracelib/kick_fifo.h"
#include "util/circular_buffer.h"
#include "xbdm.h"
#include "xbox_helper.h"

#define VERBOSE_DEBUG

#ifdef VERBOSE_DEBUG
#define VERBOSE_PRINT(c) DbgPrint c
#else
#define VERBOSE_PRINT(c)
#endif

typedef enum TracerRequest {
  REQ_NONE,
  REQ_WAIT_FOR_STABLE_PUSH_BUFFER,
  REQ_DISCARD_UNTIL_FRAME_START,
  REQ_DISCARD_UNTIL_NEXT_FLIP,
  REQ_TRACE_UNTIL_FLIP,
} TracerRequest;

typedef struct TracerStateMachine {
  HANDLE processor_thread;
  DWORD processor_thread_id;

  CRITICAL_SECTION state_critical_section;
  TracerState state;
  TracerRequest request;

  BOOL dma_addresses_valid;
  uint32_t real_dma_pull_addr;
  uint32_t real_dma_push_addr;
  uint32_t target_dma_push_addr;

  NotifyStateChangedHandler on_notify_state_changed;
  NotifyRequestProcessedHandler on_notify_request_processed;
  NotifyBytesAvailableHandler on_pgraph_buffer_bytes_available;
  NotifyBytesAvailableHandler on_graphics_buffer_bytes_available;

  TracerConfig config;
  CRITICAL_SECTION pgraph_critical_section;
  CircularBuffer pgraph_buffer;
  CRITICAL_SECTION graphics_critical_section;
  CircularBuffer graphics_buffer;
} TracerStateMachine;

// Function signature for pre/post processing callbacks.
typedef void (*PGRAPHCommandCallback)(const PushBufferCommandTraceInfo *info);

typedef struct PGRAPHCommandProcessor {
  BOOL valid;

  // The command to be processed.
  uint32_t command;

  PGRAPHCommandCallback *pre_callbacks;
  PGRAPHCommandCallback *post_callbacks;
} PGRAPHCommandProcessor;

typedef struct PGRAPHClassProcessor {
  uint32_t class;
  PGRAPHCommandProcessor *processors;
} PGRAPHClassProcessor;

static const uint32_t kTag = 0x6E74534D;  // 'ntSM'

// Maximum number of bytes to leave in the FIFO before allowing it to be
// processed. A cap is necessary to prevent Direct3D from performing fixups that
// would not happen outside of tracing conditions.
static const uint32_t kMaxQueueDepthBeforeFlush = 200;

static const PGRAPHClassProcessor kPGRAPHProcessorRegistry[] = {{0, NULL}};

static TracerStateMachine state_machine = {0};

static DWORD __attribute__((stdcall))
TracerThreadMain(LPVOID lpThreadParameter);

static void SetState(TracerState new_state);
static TracerRequest GetRequest(void);
static BOOL SetRequest(TracerRequest new_request);
static void Shutdown(void);

static void WaitForStablePushBufferState(void);
static void DiscardUntilFramebufferFlip(BOOL require_new_frame);
static void TraceUntilFramebufferFlip(BOOL discard);

static void *Allocator(size_t size) {
  return DmAllocatePoolWithTag(size, kTag);
}

static void Free(void *block) { return DmFreePool(block); }

HRESULT TracerInitialize(
    NotifyStateChangedHandler on_notify_state_changed,
    NotifyRequestProcessedHandler on_notify_request_processed,
    NotifyBytesAvailableHandler on_pgraph_buffer_bytes_available,
    NotifyBytesAvailableHandler on_graphics_buffer_bytes_available) {
  if (!on_notify_state_changed) {
    DbgPrint("Invalid on_notify_state_changed handler.");
    return XBOX_E_FAIL;
  }

  state_machine.on_notify_state_changed = on_notify_state_changed;
  state_machine.on_notify_request_processed = on_notify_request_processed;
  state_machine.on_pgraph_buffer_bytes_available =
      on_pgraph_buffer_bytes_available;
  state_machine.on_graphics_buffer_bytes_available =
      on_graphics_buffer_bytes_available;

  state_machine.state = STATE_UNINITIALIZED;
  InitializeCriticalSection(&state_machine.state_critical_section);
  InitializeCriticalSection(&state_machine.pgraph_critical_section);
  InitializeCriticalSection(&state_machine.graphics_critical_section);

  return XBOX_S_OK;
}

void TracerGetDefaultConfig(TracerConfig *config) {
  config->pgraph_circular_buffer_size = 1024 * 16;
  config->graphics_circular_buffer_size = 1920 * 1080 * 4 * 2;

  config->rdi_capture_enabled = FALSE;
  config->surface_color_capture_enabled = TRUE;
  config->surface_depth_capture_enabled = FALSE;
  config->texture_capture_enabled = TRUE;
}

HRESULT TracerCreate(const TracerConfig *config) {
  DbgPrint("TracerCreate: %d", state_machine.state);

  if (state_machine.state > STATE_UNINITIALIZED) {
    DbgPrint("Unexpected state %d in TracerCreate", state_machine.state);
    return XBOX_E_EXISTS;
  }

  SetState(STATE_INITIALIZING);
  state_machine.config = *config;
  state_machine.request = REQ_NONE;

  if (config->rdi_capture_enabled || config->surface_color_capture_enabled ||
      config->surface_depth_capture_enabled ||
      config->texture_capture_enabled) {
    state_machine.graphics_buffer =
        CBCreateEx(config->graphics_circular_buffer_size, Allocator, Free);
    if (!state_machine.graphics_buffer) {
      return XBOX_E_ACCESS_DENIED;
    }
  }
  state_machine.pgraph_buffer =
      CBCreateEx(config->pgraph_circular_buffer_size, Allocator, Free);
  if (!state_machine.pgraph_buffer) {
    CBDestroy(state_machine.graphics_buffer);
    return XBOX_E_ACCESS_DENIED;
  }

  state_machine.processor_thread = CreateThread(
      NULL, 0, TracerThreadMain, NULL, 0, &state_machine.processor_thread_id);
  if (!state_machine.processor_thread) {
    SetState(STATE_UNINITIALIZED);
    CBDestroy(state_machine.graphics_buffer);
    CBDestroy(state_machine.pgraph_buffer);
    return XBOX_E_FAIL;
  }

  SetState(STATE_INITIALIZED);

  return XBOX_S_OK;
}

void TracerShutdown(void) {
  if (state_machine.state == STATE_UNINITIALIZED) {
    return;
  }

  TracerState state = TracerGetState();
  if (state == STATE_SHUTDOWN) {
    return;
  }

  SetState(STATE_SHUTDOWN_REQUESTED);
}

TracerState TracerGetState(void) {
  EnterCriticalSection(&state_machine.state_critical_section);
  TracerState ret = state_machine.state;
  LeaveCriticalSection(&state_machine.state_critical_section);
  return ret;
}

BOOL TracerGetDMAAddresses(uint32_t *push_addr, uint32_t *pull_addr) {
  EnterCriticalSection(&state_machine.state_critical_section);
  *push_addr = state_machine.real_dma_push_addr;
  *pull_addr = state_machine.real_dma_pull_addr;
  BOOL ret = state_machine.dma_addresses_valid;
  LeaveCriticalSection(&state_machine.state_critical_section);

  return ret;
}

static void NotifyStateChanged(TracerState new_state) {
  if (!state_machine.on_notify_state_changed) {
    return;
  }
  state_machine.on_notify_state_changed(new_state);
}

static void NotifyRequestProcessed() {
  if (!state_machine.on_notify_request_processed) {
    return;
  }
  state_machine.on_notify_request_processed();
}

static void SetState(TracerState new_state) {
  EnterCriticalSection(&state_machine.state_critical_section);
  BOOL changed = state_machine.state != new_state;
  state_machine.state = new_state;
  LeaveCriticalSection(&state_machine.state_critical_section);

  if (changed) {
    NotifyStateChanged(new_state);
  }
}

static TracerRequest GetRequest(void) {
  EnterCriticalSection(&state_machine.state_critical_section);
  TracerRequest ret = state_machine.request;
  LeaveCriticalSection(&state_machine.state_critical_section);
  return ret;
}

static void CompleteRequest(void) {
  EnterCriticalSection(&state_machine.state_critical_section);
  state_machine.request = REQ_NONE;
  LeaveCriticalSection(&state_machine.state_critical_section);
}

static BOOL SetRequest(TracerRequest new_request) {
  BOOL ret;
  EnterCriticalSection(&state_machine.state_critical_section);
  TracerRequest current = state_machine.request;
  if (current != REQ_NONE && current != new_request) {
    ret = FALSE;
  } else {
    state_machine.request = new_request;
    ret = TRUE;
  }
  LeaveCriticalSection(&state_machine.state_critical_section);

  if (!ret) {
    DbgPrint("Attempt to set request to %d but already %d", new_request,
             current);
  }
  return ret;
}

BOOL TracerIsProcessingRequest(void) {
  EnterCriticalSection(&state_machine.state_critical_section);
  BOOL ret = state_machine.request != REQ_NONE;
  LeaveCriticalSection(&state_machine.state_critical_section);
  return ret;
}

static void SaveDMAAddresses(uint32_t push_addr, uint32_t pull_addr) {
  EnterCriticalSection(&state_machine.state_critical_section);
  state_machine.real_dma_pull_addr = pull_addr;
  state_machine.real_dma_push_addr = push_addr;
  state_machine.dma_addresses_valid = TRUE;
  LeaveCriticalSection(&state_machine.state_critical_section);
}

HRESULT TracerBeginWaitForStablePushBufferState(void) {
  if (SetRequest(REQ_WAIT_FOR_STABLE_PUSH_BUFFER)) {
    return XBOX_S_OK;
  }
  return XBOX_E_ACCESS_DENIED;
}

HRESULT TracerBeginDiscardUntilFlip(BOOL require_new_frame) {
  TracerRequest request = require_new_frame ? REQ_DISCARD_UNTIL_NEXT_FLIP
                                            : REQ_DISCARD_UNTIL_FRAME_START;
  if (SetRequest(request)) {
    return XBOX_S_OK;
  }
  return XBOX_E_ACCESS_DENIED;
}

HRESULT TracerTraceCurrentFrame(void) {
  if (SetRequest(REQ_TRACE_UNTIL_FLIP)) {
    return XBOX_S_OK;
  }
  return XBOX_E_ACCESS_DENIED;
}

uint32_t TracerLockPGRAPHBuffer(void) {
  EnterCriticalSection(&state_machine.pgraph_critical_section);
  return CBAvailable(state_machine.pgraph_buffer);
}

uint32_t TracerReadPGRAPHBuffer(void *buffer, uint32_t size) {
  return CBReadAvailable(state_machine.pgraph_buffer, buffer, size);
}
//! Releases the lock on the PGRAPH buffer.
void TracerUnlockPGRAPHBuffer(void) {
  LeaveCriticalSection(&state_machine.pgraph_critical_section);
}

//! Locks the Graphics buffer to prevent writing, returning the bytes available
//! in the buffer.
uint32_t TracerLockGraphicsBuffer(void) {
  EnterCriticalSection(&state_machine.graphics_critical_section);
  return CBAvailable(state_machine.graphics_buffer);
}

uint32_t TracerReadGraphicsBuffer(void *buffer, uint32_t size) {
  return CBReadAvailable(state_machine.graphics_buffer, buffer, size);
}

void TracerUnlockGraphicsBuffer(void) {
  LeaveCriticalSection(&state_machine.graphics_critical_section);
}

static DWORD __attribute__((stdcall))
TracerThreadMain(LPVOID lpThreadParameter) {
  while (TracerGetState() == STATE_INITIALIZING) {
    Sleep(1);
  }

  // Check for any failures between the time the thread was created and the time
  // it started running.
  if (TracerGetState() != STATE_INITIALIZED) {
    Shutdown();
    return 0;
  }

  SetState(STATE_IDLE);

  while (1) {
    TracerState state = TracerGetState();
    if (state < STATE_INITIALIZING) {
      break;
    }

    TracerRequest request = GetRequest();
    switch (request) {
      case REQ_WAIT_FOR_STABLE_PUSH_BUFFER:
        WaitForStablePushBufferState();
        NotifyRequestProcessed();
        break;

      case REQ_DISCARD_UNTIL_FRAME_START:
        DiscardUntilFramebufferFlip(FALSE);
        NotifyRequestProcessed();
        break;

      case REQ_DISCARD_UNTIL_NEXT_FLIP:
        DiscardUntilFramebufferFlip(TRUE);
        NotifyRequestProcessed();
        break;

      case REQ_TRACE_UNTIL_FLIP:
        TraceUntilFramebufferFlip(false);
        NotifyRequestProcessed();
        break;

      case REQ_NONE:
        break;
    }

    Sleep(10);
  }

  Shutdown();
  return 0;
}

static void Shutdown(void) {
  if (state_machine.dma_addresses_valid) {
    // Recover the real address
    SetDMAPushAddress(state_machine.real_dma_push_addr);
    state_machine.dma_addresses_valid = FALSE;
  }

  // We can continue the cache updates now.
  ResumeFIFOPusher();

  CBDestroy(state_machine.graphics_buffer);
  CBDestroy(state_machine.pgraph_buffer);

  SetState(STATE_SHUTDOWN);

  DeleteCriticalSection(&state_machine.state_critical_section);
  DeleteCriticalSection(&state_machine.pgraph_critical_section);
  DeleteCriticalSection(&state_machine.graphics_critical_section);
}

static void WaitForStablePushBufferState(void) {
  TracerState current_state = TracerGetState();
  if (current_state == STATE_IDLE_STABLE_PUSH_BUFFER ||
      current_state == STATE_IDLE_NEW_FRAME) {
    NotifyStateChanged(current_state);
    CompleteRequest();
    return;
  }

  SetState(STATE_WAITING_FOR_STABLE_PUSH_BUFFER);

  uint32_t dma_pull_addr = 0;
  uint32_t dma_push_addr_real = 0;

  while (TracerGetState() == STATE_WAITING_FOR_STABLE_PUSH_BUFFER) {
    // Stop consuming CACHE entries.
    DisablePGRAPHFIFO();
    BusyWaitUntilPGRAPHIdle();

    // Kick the pusher so that it fills the CACHE.
    MaybePopulateFIFOCache(0);

    // Now drain the CACHE.
    EnablePGRAPHFIFO();

    // Check out where the PB currently is and where it was supposed to go.
    dma_push_addr_real = GetDMAPushAddress();
    dma_pull_addr = GetDMAPullAddress();

    // Check if we have any methods left to run and skip those.
    DMAState dma_state;
    GetDMAState(&dma_state);
    dma_pull_addr += dma_state.method_count * 4;

    // Hide all commands from the PB by setting PUT = GET.
    uint32_t dma_push_addr_target = dma_pull_addr;
    SetDMAPushAddress(dma_push_addr_target);

    // Resume pusher - The PB can't run yet, as it has no commands to process.
    ResumeFIFOPusher();

    // We might get issues where the pusher missed our PUT (miscalculated).
    // This can happen as `dma_method_count` is not the most accurate.
    // Probably because the DMA is halfway through a transfer.
    // So we pause the pusher again to validate our state
    PauseFIFOPusher();

    // TODO: Determine whether a sleep is needed and optimize the value.
    Sleep(1000);

    uint32_t dma_push_addr_check = GetDMAPushAddress();
    uint32_t dma_pull_addr_check = GetDMAPullAddress();

    // We want the PB to be empty.
    if (dma_pull_addr_check != dma_push_addr_check) {
      DbgPrint("Pushbuffer not empty - PULL (0x%08X) != PUSH (0x%08X)\n",
               dma_pull_addr_check, dma_push_addr_check);
      continue;
    }

    // Ensure that we are at the correct offset
    if (dma_push_addr_check != dma_push_addr_target) {
      DbgPrint("Oops PUT was modified; got 0x%08X but expected 0x%08X!\n",
               dma_push_addr_check, dma_push_addr_target);
      continue;
    }

    SaveDMAAddresses(dma_push_addr_real, dma_pull_addr);
    state_machine.target_dma_push_addr = dma_pull_addr;
    SetState(STATE_IDLE_STABLE_PUSH_BUFFER);
    CompleteRequest();
    return;
  }

  DbgPrint("Wait for idle aborted, restoring PFIFO state...\n");
  SetDMAPushAddress(dma_push_addr_real);
  EnablePGRAPHFIFO();
  ResumeFIFOPusher();

  SaveDMAAddresses(dma_push_addr_real, dma_pull_addr);
}

// Sets the DMA_PUSH_ADDR to the given target, storing the old value.
static void ExchangeDMAPushAddress(uint32_t target) {
  EnterCriticalSection(&state_machine.state_critical_section);
  uint32_t prev_target = state_machine.target_dma_push_addr;

  uint32_t real = ExchangeDWORD(DMA_PUSH_ADDR, target);
  state_machine.target_dma_push_addr = target;

  // It must point where we pointed previously, otherwise something is broken.
  if (real != prev_target) {
    uint32_t push_state = ReadDWORD(CACHE_PUSH_STATE);
    if (push_state & 0x01) {
      DbgPrint("PUT was modified and pusher was already active!\n");
      Sleep(60 * 1000);
    }

    state_machine.real_dma_push_addr = real;
  }
  LeaveCriticalSection(&state_machine.state_critical_section);
}

// Runs the PFIFO until the DMA_PULL_ADDR equals the given address.
static void RunFIFO(uint32_t pull_addr_target) {
  // Mark the pushbuffer as empty by setting the push address to the target pull
  // address.
  ExchangeDMAPushAddress(pull_addr_target);
  // FIXME: we can avoid this read in some cases, as we should know where we are
  state_machine.real_dma_pull_addr = GetDMAPullAddress();

  // Loop while this command is being run.
  // This is necessary because a whole command might not fit into CACHE.
  // So we have to process it chunk by chunk.
  // FIXME: This used to be a check which made sure that `dma_pull_addr` did
  //       never leave the known PB.
  uint32_t iterations_with_no_change = 0;
  while (state_machine.real_dma_pull_addr != pull_addr_target) {
    if (iterations_with_no_change && !(iterations_with_no_change % 1000)) {
      DbgPrint(
          "Warning: %d iterations with no change to DMA_PULL_ADDR 0x%X "
          " target 0x%X\n",
          iterations_with_no_change, state_machine.real_dma_pull_addr,
          pull_addr_target);
    }

    VERBOSE_PRINT(
        ("RunFIFO: At 0x%08X, target is 0x%08X (Real: 0x%08X)\n"
         "         PULL ADDR: 0x%X  PUSH: 0x%X\n",
         state_machine.real_dma_pull_addr, pull_addr_target,
         state_machine.real_dma_push_addr, GetDMAPullAddress(),
         GetDMAPushAddress()));

    // Disable PGRAPH, so it can't run anything from CACHE.
    DisablePGRAPHFIFO();
    BusyWaitUntilPGRAPHIdle();

    // This scope should be atomic.
    // FIXME: Avoid running bad code if PUT was modified during this command.
    ExchangeDMAPushAddress(pull_addr_target);

    // FIXME: xemu does not seem to implement the CACHE behavior
    // This leads to an infinite loop as the kick fails to populate the cache.
    KickResult result = KickFIFO(pull_addr_target);
    for (int32_t i = 0; result == KICK_TIMEOUT && i < 64; ++i) {
      result = KickFIFO(pull_addr_target);
    }
    if (result != KICK_OK && result != KICK_TIMEOUT) {
      DbgPrint("Warning: FIFO kick failed: %d\n", result);
    }

    // Run the commands we have moved to CACHE, by enabling PGRAPH.
    EnablePGRAPHFIFO();
    Sleep(10);

    // Get the updated PB address.
    uint32_t new_get_addr = GetDMAPullAddress();
    if (new_get_addr == state_machine.real_dma_pull_addr) {
      iterations_with_no_change += 1;
    } else {
      state_machine.real_dma_pull_addr = new_get_addr;
      iterations_with_no_change = 0;
    }
  }

  // This is just to confirm that nothing was modified in the final chunk.
  ExchangeDMAPushAddress(pull_addr_target);
}

// Looks up any registered processors for the given PushBufferCommandTraceInfo.
static void GetMethodProcessors(const PushBufferCommandTraceInfo *method_info,
                                PGRAPHCommandCallback **pre_callbacks,
                                PGRAPHCommandCallback **post_callbacks) {
  *pre_callbacks = NULL;
  *post_callbacks = NULL;
  if (!method_info->valid) {
    return;
  }

  const PGRAPHClassProcessor *class_registry = kPGRAPHProcessorRegistry;
  while (class_registry->processors &&
         class_registry->class != method_info->graphics_class) {
    ++class_registry;
  }

  if (!class_registry->processors) {
    return;
  }

  const PGRAPHCommandProcessor *entry = class_registry->processors;
  while (entry->valid) {
    if (entry->command == method_info->command.method) {
      *pre_callbacks = entry->pre_callbacks;
      *post_callbacks = entry->post_callbacks;
      return;
    }
    ++entry;
  }
}

static uint32_t ProcessPushBufferCommand(
    uint32_t *dma_pull_addr, PushBufferCommandTraceInfo *method_info,
    BOOL discard, BOOL skip_hooks) {
  method_info->valid = FALSE;
  uint32_t unprocessed_bytes = 0;

  if (*dma_pull_addr == state_machine.real_dma_push_addr) {
    return 0;
  }

  uint32_t post_addr =
      ParsePushBufferCommandTraceInfo(*dma_pull_addr, method_info, discard);
  if (!post_addr) {
    DeletePushBufferCommandTraceInfo(method_info);
    return 0xFFFFFFFF;
  }

  if (!method_info->valid) {
    DbgPrint("WARNING: No method. Going to 0x%08X", post_addr);
    unprocessed_bytes = 4;
  } else {
    // Calculate the size of the instruction + any associated parameters.
    unprocessed_bytes = 4 + method_info->command.parameter_count * 4;

    PGRAPHCommandCallback *pre_callbacks = NULL;
    PGRAPHCommandCallback *post_callbacks = NULL;
    if (!skip_hooks) {
      GetMethodProcessors(method_info, &pre_callbacks, &post_callbacks);
    }

    if (pre_callbacks) {
      // Go where we can do pre-callback.
      RunFIFO(*dma_pull_addr);

      // Do the pre callbacks before running the command
      // FIXME: assert we are where we wanted to be
      while (pre_callbacks) {
        (*pre_callbacks)(method_info);
      }
    }

    if (post_callbacks) {
      // If we reached target, we can't step again without leaving valid buffer

      if (*dma_pull_addr != state_machine.real_dma_push_addr) {
        DbgPrint("ERROR: Bad state in ProcessPushBufferCommand: 0x%X != 0x%X\n",
                 *dma_pull_addr, state_machine.real_dma_push_addr);
        return 0xFFFFFFFF;
      }

      // Go where we want to go (equivalent to step)
      RunFIFO(post_addr);

      // We have processed all bytes now
      unprocessed_bytes = 0;

      while (post_callbacks) {
        (*post_callbacks)(method_info);
      }
    }
    //      // Add the pushbuffer command to log
    //      self._record_push_buffer_command(method_info, pre_info, post_info)
  }

  *dma_pull_addr = post_addr;

  return unprocessed_bytes;
}

//! Write all of the given data to the given circular buffer.
static void WriteBuffer(NotifyBytesAvailableHandler notify_bytes_available,
                        CRITICAL_SECTION *critical_section, CircularBuffer cb,
                        const void *data, uint32_t len) {
  while (len) {
    EnterCriticalSection(critical_section);
    uint32_t bytes_written = CBWriteAvailable(cb, data, len);
    uint32_t bytes_available = CBAvailable(cb);
    LeaveCriticalSection(critical_section);

    len -= bytes_written;
    notify_bytes_available(bytes_available);

    if (len) {
      VERBOSE_PRINT(("WriteBuffer: Circular buffer full, sleeping...\n"));
      Sleep(10);
    }
  }
}

static void LogCommand(const PushBufferCommandTraceInfo *info) {
  if (!info->valid) {
    return;
  }
  WriteBuffer(state_machine.on_pgraph_buffer_bytes_available,
              &state_machine.pgraph_critical_section,
              state_machine.pgraph_buffer, info, sizeof(*info));
  if (info->data && info->command.parameter_count) {
    uint32_t data_size = info->command.parameter_count * 4;
    WriteBuffer(state_machine.on_pgraph_buffer_bytes_available,
                &state_machine.pgraph_critical_section,
                state_machine.pgraph_buffer, info->data, data_size);
  }
}

//! Attempts to find a FLIP_STALL in the FIFO buffer, setting the `found`
//! parameter to `TRUE` if one is found.
//!
//! \return FALSE on fatal error, otherwise TRUE.
static BOOL PeekAheadForFlipStall(BOOL *found, uint32_t dma_pull_addr,
                                  uint32_t real_dma_push_addr) {
  // TODO: Handle the case where an inc happens near the end of the
  // buffer.
  //   Hold off on detecting the flip and force an additional read.
  *found = FALSE;

  uint32_t peek_dma_pull_addr = dma_pull_addr;
  for (uint32_t i = 0; i < 5 && peek_dma_pull_addr != real_dma_push_addr; ++i) {
    PushBufferCommandTraceInfo info;
    uint32_t peek_unprocessed_bytes =
        ProcessPushBufferCommand(&peek_dma_pull_addr, &info, TRUE, TRUE);

    if (peek_unprocessed_bytes == 0xFFFFFFFF) {
      DbgPrint("Failed to process pbuffer command during seek.\n");
      SetState(STATE_FATAL_PROCESS_PUSH_BUFFER_COMMAND_FAILED);
      return FALSE;
    }

    if (info.valid && info.graphics_class == 0x97 &&
        info.command.method == NV097_FLIP_STALL) {
      VERBOSE_PRINT(
          ("Found FLIP_STALL after FLIP_INC after peeking %d "
           "commands.\n",
           i + 1));
      *found = TRUE;
      return TRUE;
    }

    if (!peek_unprocessed_bytes) {
      return TRUE;
    }
  }
  return TRUE;
}

static void TraceUntilFramebufferFlip(BOOL discard) {
  TracerState current_state = TracerGetState();
  if (!discard && current_state != STATE_IDLE_NEW_FRAME) {
    SetState(STATE_FATAL_NOT_IN_NEW_FRAME_STATE);
    CompleteRequest();
    return;
  }

  TracerState working_state =
      discard ? STATE_DISCARDING_UNTIL_FLIP : STATE_TRACING_UNTIL_FLIP;
  SetState(working_state);

  uint32_t bytes_queued = 0;
  uint32_t dma_pull_addr = state_machine.real_dma_pull_addr;
  uint32_t commands_discarded = 0;

  uint32_t command_index = 1;

  uint32_t last_push_addr = 0;
  uint32_t sleep_millis = 5;
  uint32_t sleep_calls = 0;

  while (TracerGetState() == working_state) {
    PushBufferCommandTraceInfo info;
    info.packet_index = command_index++;
    uint32_t unprocessed_bytes =
        ProcessPushBufferCommand(&dma_pull_addr, &info, discard, TRUE);
    if (unprocessed_bytes == 0xFFFFFFFF) {
      SetState(STATE_FATAL_PROCESS_PUSH_BUFFER_COMMAND_FAILED);
      CompleteRequest();
      return;
    }
    bytes_queued += unprocessed_bytes;

    uint32_t real_dma_push_addr;
    {
      uint32_t real_dma_pull_addr;
      if (!TracerGetDMAAddresses(&real_dma_push_addr, &real_dma_pull_addr)) {
        DbgPrint("DMA Addresses invalid inside trace loop!\n");
        real_dma_push_addr = 0;
      }
    }

    BOOL is_flip = FALSE;
    BOOL is_empty = dma_pull_addr == real_dma_push_addr;

    if (info.valid && info.graphics_class == 0x97) {
      is_flip = info.command.method == NV097_FLIP_STALL;

      // The nxdk does not trigger a FLIP_STALL, but does do an increment.
      // XDK-based titles do both an increment and a stall shortly after.
      // On detection of an increment, a few commands are peeked to guess
      // whether this is an nxdk title or an XDK one where the inc should not be
      // considered a flip.
      if (info.command.method == NV097_FLIP_INCREMENT_WRITE) {
        VERBOSE_PRINT(("Found FLIP_INC, seeking FLIP_STALL!\n"));
        BOOL flip_found = FALSE;
        if (!PeekAheadForFlipStall(&flip_found, dma_pull_addr,
                                   real_dma_push_addr)) {
          CompleteRequest();
          return;
        }
        is_flip = !flip_found;
        VERBOSE_PRINT(
            ("Exited FLIP_STALL search, treat FLIP_INC as stall? %s\n",
             is_flip ? "YES" : "NO"));
      }
    }

#ifdef VERBOSE_DEBUG
    if (discard && info.valid && info.graphics_class == 0x97) {
      DbgPrint("Discarding command 0x%X - 0x%X\n", info.graphics_class,
               info.command.method);
    }
#endif  // VERBOSE_DEBUG

    // Avoid queuing up too many bytes: while the buffer is being processed,
    // D3D might fixup the buffer if GET is still too far away.
    if (is_empty || is_flip || bytes_queued >= kMaxQueueDepthBeforeFlush) {
      if (!is_empty) {
        DbgPrint(
            "Tracer: Flushing buffer until (0x%08X): real_put 0x%X; "
            "bytes_queued: %d\n",
            dma_pull_addr, real_dma_push_addr, bytes_queued);
      } else {
        DbgPrint(
            "Tracer: No PGRAPH commands available. Real push: 0x%08X, live "
            "push: 0x%08X, live pull: 0x%08X\n",
            real_dma_push_addr, GetDMAPushAddress(), GetDMAPullAddress());
      }

      RunFIFO(dma_pull_addr);
      bytes_queued = 0;
    }

    // Verify we are where we think we are
    if (!bytes_queued) {
      uint32_t dma_pull_addr_real = GetDMAPullAddress();
      if (dma_pull_addr_real != dma_pull_addr) {
        DbgPrint(
            "ERROR: Corrupt state. HW (0x%08X) is not at parser (0x%08X)\n",
            dma_pull_addr_real, dma_pull_addr);
        SetState(STATE_FATAL_DISCARDING_FAILED);
        CompleteRequest();
        return;
      }
    }

    if (!discard) {
      LogCommand(&info);
    }

    if (is_flip) {
      SetState(STATE_IDLE_NEW_FRAME);
      CompleteRequest();
      return;
    }

    if (is_empty) {
      if (last_push_addr == real_dma_push_addr) {
        sleep_millis += 10;
        if (sleep_millis > 250 && discard) {
          sleep_millis = 5;
          DbgPrint("Permanent stall detected, attempting to populate FIFO\n");
          // NOTE: EnableFIFO + ResumePusher + PausePusher + DisableFIFO is
          // insufficient to fix this problem.
          EnablePGRAPHFIFO();
          MaybePopulateFIFOCache(10);
          DisablePGRAPHFIFO();
        }
      } else {
        last_push_addr = real_dma_push_addr;
        sleep_millis = 5;
      }
      if (!(++sleep_calls & 0x7F)) {
        DbgPrint("Still waiting for new pgraph commands after %u iterations\n",
                 sleep_calls);
      }
      DbgPrint("Reached end of buffer with %d bytes queued - sleeping %d ms\n",
               bytes_queued, sleep_millis);
      Sleep(sleep_millis);
    } else {
      sleep_calls = 0;
#ifdef VERBOSE_DEBUG
      if (discard && !(++commands_discarded & 0x01FF)) {
        DbgPrint(
            "Awaiting flip stall... Discarded %d commands   PULL@ 0x%X  "
            "REAL_PUSH: 0x%X  BQ: %d  MTHD: 0x%X\n",
            commands_discarded, dma_pull_addr, real_dma_push_addr, bytes_queued,
            info.command.method);
      }
#endif
    }
  }
}

static void DiscardUntilFramebufferFlip(BOOL require_new_frame) {
  TracerState current_state = TracerGetState();
  if (!require_new_frame && current_state == STATE_IDLE_NEW_FRAME) {
    NotifyStateChanged(current_state);
    CompleteRequest();
    return;
  }

  if (!state_machine.dma_addresses_valid) {
    SetState(STATE_FATAL_NOT_IN_STABLE_STATE);
    return;
  }

  TraceUntilFramebufferFlip(true);
}
