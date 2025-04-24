#include "tracer_state_machine.h"

#include "exchange_dword.h"
#include "kick_fifo.h"
#include "pgraph_command_callbacks.h"
#include "pushbuffer_command.h"
#include "register_defs.h"
#include "util/circular_buffer.h"
#include "xbdm.h"
#include "xbox_helper.h"

// #define VERBOSE_DEBUG

#ifdef VERBOSE_DEBUG
#define VERBOSE_PRINT(c) DbgPrint c
#else
#define VERBOSE_PRINT(c)
#endif

// #define ENABLE_PROFILING

#ifdef ENABLE_PROFILING
#include "util/profiler.h"

#define PROFILE_INIT() PROFILETOKEN __now
#define PROFILE_START() __now = ProfileStart()
#define PROFILE_SEND(msg)                                       \
  do {                                                          \
    double __elapsed = ProfileStop(&__now);                     \
    uint32_t __milliseconds = (uint32_t)__elapsed;              \
    uint32_t __fractional_milliseconds =                        \
        (uint32_t)((__elapsed - __milliseconds) * 1000.0);      \
    DbgPrint("PROFILE>> %s: %u.%u ms\n", (msg), __milliseconds, \
             __fractional_milliseconds);                        \
  } while (0)
#else
#define PROFILE_INIT()
#define PROFILE_START()
#define PROFILE_SEND(msg)
#endif

#define DEFAULT_PGRAPH_BUFFER_SIZE (1024 * 64)
#define MIN_PGRAPH_BUFFER_SIZE (256)
//! The percentage of the PGRAPH circular buffer that must be filled before a
//! notification is sent.
#define PGRAPH_NOTIFY_PERCENT 0.8f
#define DEFAULT_AUX_BUFFER_SIZE (1024 * 1024 * 4)
#define MIN_AUX_BUFFER_SIZE (1024 * 512)

// Maximum number of sleep/kick attempts before permanently failing FIFO
// population.
#define MAX_STALL_WORKAROUNDS 32

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
  NotifyBytesAvailableHandler on_aux_buffer_bytes_available;

  TracerConfig config;
  CRITICAL_SECTION pgraph_critical_section;
  CircularBuffer pgraph_buffer;
  // The number of bytes that must be written to the pgraph_buffer before a
  // notification is sent. This is used to reduce chatter as pgraph entries are
  // very small and frequent.
  uint32_t pgraph_buffer_notify_threshold;
  CRITICAL_SECTION aux_critical_section;
  CircularBuffer aux_buffer;
} TracerStateMachine;

//! Describes a callback that may be called before/after a PGRAPH command is
//! processed.
typedef void (*PGRAPHCommandCallback)(const PushBufferCommandTraceInfo *info,
                                      StoreAuxData store,
                                      const AuxConfig *config);

typedef struct PGRAPHCommandProcessor {
  //! Whether or not this struct is valid.
  BOOL valid;

  //! The method ID to be processed.
  uint32_t command;

  //! Optional callback to be invoked before processing the command.
  PGRAPHCommandCallback pre_callback;
  //! Optional callback to be invoked after processing the command.
  PGRAPHCommandCallback post_callback;
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

static TracerStateMachine state_machine = {0};

static DWORD __attribute__((stdcall)) TracerThreadMain(
    LPVOID lpThreadParameter);

static void SetState(TracerState new_state);
static TracerRequest GetRequest(void);
static BOOL SetRequest(TracerRequest new_request);
static void Shutdown(void);

static void WaitForStablePushBufferState(void);
static void DiscardUntilFramebufferFlip(BOOL require_new_frame);
static void TraceUntilFramebufferFlip(BOOL discard);

#define HOOK_METHOD(cmd, pre_cb, post_cb) \
  { TRUE, cmd, pre_cb, post_cb }

#define HOOK_END() \
  { FALSE, 0, NULL, NULL }

static PGRAPHCommandProcessor kClass97Processors[] = {
    HOOK_METHOD(NV097_CLEAR_SURFACE, NULL, TraceSurfaces),
    HOOK_METHOD(NV097_BACK_END_WRITE_SEMAPHORE_RELEASE, NULL, TraceSurfaces),
    HOOK_METHOD(NV097_SET_BEGIN_END, TraceBegin, TraceEnd),
    HOOK_END(),
};

static const PGRAPHClassProcessor kPGRAPHProcessorRegistry[] = {
    {0x97, kClass97Processors},
    {0, NULL},
};

#undef HOOK_METHOD
#undef HOOK_END

static void *Allocator(size_t size) {
  return DmAllocatePoolWithTag(size, kTag);
}

static void Free(void *block) { return DmFreePool(block); }

HRESULT TracerInitialize(
    NotifyStateChangedHandler on_notify_state_changed,
    NotifyRequestProcessedHandler on_notify_request_processed,
    NotifyBytesAvailableHandler on_pgraph_buffer_bytes_available,
    NotifyBytesAvailableHandler on_aux_buffer_bytes_available) {
  if (!on_notify_state_changed) {
    DbgPrint("ERROR: Invalid on_notify_state_changed handler.");
    return XBOX_E_FAIL;
  }

  state_machine.on_notify_state_changed = on_notify_state_changed;
  state_machine.on_notify_request_processed = on_notify_request_processed;
  state_machine.on_pgraph_buffer_bytes_available =
      on_pgraph_buffer_bytes_available;
  state_machine.on_aux_buffer_bytes_available = on_aux_buffer_bytes_available;

  state_machine.state = STATE_UNINITIALIZED;
  InitializeCriticalSection(&state_machine.state_critical_section);
  InitializeCriticalSection(&state_machine.pgraph_critical_section);
  InitializeCriticalSection(&state_machine.aux_critical_section);

  return XBOX_S_OK;
}
void TracerGetDefaultConfig(TracerConfig *config) {
  config->pgraph_circular_buffer_size = DEFAULT_PGRAPH_BUFFER_SIZE;
  config->aux_circular_buffer_size = DEFAULT_AUX_BUFFER_SIZE;

  config->aux_tracing_config.raw_pgraph_capture_enabled = FALSE;
  config->aux_tracing_config.raw_pfb_capture_enabled = FALSE;
  config->aux_tracing_config.rdi_capture_enabled = FALSE;
  config->aux_tracing_config.surface_color_capture_enabled = TRUE;
  config->aux_tracing_config.surface_depth_capture_enabled = FALSE;
  config->aux_tracing_config.texture_capture_enabled = TRUE;
}

static BOOL AuxCaptureEnabled(const AuxConfig *config) {
  return config->raw_pgraph_capture_enabled ||
         config->raw_pfb_capture_enabled || config->rdi_capture_enabled ||
         config->surface_color_capture_enabled ||
         config->surface_depth_capture_enabled ||
         config->texture_capture_enabled;
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

  if (AuxCaptureEnabled(&config->aux_tracing_config)) {
    uint32_t buffer_size = config->aux_circular_buffer_size;
    if (buffer_size < MIN_AUX_BUFFER_SIZE) {
      buffer_size = MIN_AUX_BUFFER_SIZE;
    }
    state_machine.aux_buffer = CBCreateEx(buffer_size, Allocator, Free);
    if (!state_machine.aux_buffer) {
      return XBOX_E_ACCESS_DENIED;
    }
  } else {
    state_machine.aux_buffer = NULL;
  }

  uint32_t buffer_size = config->pgraph_circular_buffer_size;
  if (buffer_size < MIN_PGRAPH_BUFFER_SIZE) {
    buffer_size = MIN_PGRAPH_BUFFER_SIZE;
  }
  state_machine.pgraph_buffer = CBCreateEx(buffer_size, Allocator, Free);
  if (!state_machine.pgraph_buffer) {
    CBDestroy(state_machine.aux_buffer);
    return XBOX_E_ACCESS_DENIED;
  }
  state_machine.pgraph_buffer_notify_threshold =
      (uint32_t)((float)buffer_size * PGRAPH_NOTIFY_PERCENT);

  state_machine.processor_thread = CreateThread(
      NULL, 0, TracerThreadMain, NULL, 0, &state_machine.processor_thread_id);
  if (!state_machine.processor_thread) {
    SetState(STATE_UNINITIALIZED);
    CBDestroy(state_machine.aux_buffer);
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
    DbgPrint("ERROR: Attempt to set request to %d but already %d", new_request,
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

//! Locks the auxiliary buffer to prevent writing, returning the bytes
//! available in the buffer.
uint32_t TracerLockAuxBuffer(void) {
  EnterCriticalSection(&state_machine.aux_critical_section);
  return CBAvailable(state_machine.aux_buffer);
}

uint32_t TracerReadAuxBuffer(void *buffer, uint32_t size) {
  return CBReadAvailable(state_machine.aux_buffer, buffer, size);
}

void TracerUnlockAuxBuffer(void) {
  LeaveCriticalSection(&state_machine.aux_critical_section);
}

static DWORD __attribute__((stdcall)) TracerThreadMain(
    LPVOID lpThreadParameter) {
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

      case REQ_TRACE_UNTIL_FLIP: {
        TraceUntilFramebufferFlip(FALSE);

        uint32_t bytes_available = CBAvailable(state_machine.pgraph_buffer);
        if (bytes_available) {
          state_machine.on_pgraph_buffer_bytes_available(bytes_available);
        }

        bytes_available = CBAvailable(state_machine.aux_buffer);
        if (bytes_available) {
          state_machine.on_aux_buffer_bytes_available(bytes_available);
        }
        NotifyRequestProcessed();
      } break;

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

  CBDestroy(state_machine.aux_buffer);
  CBDestroy(state_machine.pgraph_buffer);

  SetState(STATE_SHUTDOWN);

  DeleteCriticalSection(&state_machine.state_critical_section);
  DeleteCriticalSection(&state_machine.pgraph_critical_section);
  DeleteCriticalSection(&state_machine.aux_critical_section);
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
    MaybePopulateFIFOCache(1);

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
      VERBOSE_PRINT(("Pushbuffer not empty - PULL (0x%08X) != PUSH (0x%08X)\n",
                     dma_pull_addr_check, dma_push_addr_check));
      continue;
    }

    // Ensure that we are at the correct offset
    if (dma_push_addr_check != dma_push_addr_target) {
      DbgPrint("WARNING: PUT was modified; got 0x%08X but expected 0x%08X!\n",
               dma_push_addr_check, dma_push_addr_target);
      continue;
    }

    SaveDMAAddresses(dma_push_addr_real, dma_pull_addr);
    state_machine.target_dma_push_addr = dma_pull_addr;
    SetState(STATE_IDLE_STABLE_PUSH_BUFFER);
    CompleteRequest();
    return;
  }

  DbgPrint("WARNING: Wait for idle aborted, restoring PFIFO state...\n");
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
      DbgPrint("WARNING: PUT was modified and pusher was already active!\n");
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

  if (state_machine.real_dma_pull_addr == pull_addr_target) {
    VERBOSE_PRINT(
        ("RunFIFO: Early bailout, real pull addr 0x%X == target 0x%X\n",
         state_machine.real_dma_pull_addr, pull_addr_target));
  }

  // Loop while this command is being run.
  // This is necessary because a whole command might not fit into CACHE.
  // So we have to process it chunk by chunk.
  // FIXME: This used to be a check which made sure that `dma_pull_addr` did
  //       never leave the known PB.
  uint32_t iterations_with_no_change = 0;
  while (state_machine.real_dma_pull_addr != pull_addr_target) {
    if (iterations_with_no_change && !(iterations_with_no_change % 1000)) {
      DbgPrint(
          "WARNING: %d iterations with no change to DMA_PULL_ADDR 0x%X "
          " target 0x%X\n",
          iterations_with_no_change, state_machine.real_dma_pull_addr,
          pull_addr_target);
    }

    VERBOSE_PRINT(
        ("RunFIFO: State pull=0x%08X, target=0x%08X push=0x%08X - Live: "
         "pull=0x%X push=0x%X\n",
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
    if (result != KICK_OK) {
      if (result == KICK_TIMEOUT) {
        DbgPrint("WARNING: FIFO kick timed out\n");
      } else {
        DbgPrint("WARNING: FIFO kick failed: %d\n", result);
      }
    }

    // Run the commands we have moved to CACHE, by enabling PGRAPH.
    EnablePGRAPHFIFO();

    // TODO: Verify that a simple yield is sufficient. This used to sleep 10ms.
    SwitchToThread();

    // Get the updated PB address.
    uint32_t new_get_addr = GetDMAPullAddress();
    if (new_get_addr == state_machine.real_dma_pull_addr) {
      ++iterations_with_no_change;
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
                                PGRAPHCommandCallback *pre_callback,
                                PGRAPHCommandCallback *post_callback) {
  *pre_callback = NULL;
  *post_callback = NULL;
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
      *pre_callback = entry->pre_callback;
      *post_callback = entry->post_callback;
      return;
    }
    ++entry;
  }
}

//! Write all of the given data to the given circular buffer.
static void WriteBuffer(NotifyBytesAvailableHandler notify_bytes_available,
                        CRITICAL_SECTION *critical_section, CircularBuffer cb,
                        const void *data, uint32_t len,
                        uint32_t notify_threshold) {
  PROFILE_INIT();
  PROFILE_START();
  while (len) {
    EnterCriticalSection(critical_section);
    uint32_t bytes_written = CBWriteAvailable(cb, data, len);
    uint32_t bytes_available = CBAvailable(cb);
    LeaveCriticalSection(critical_section);

    if (bytes_written) {
      len -= bytes_written;
      if (bytes_available >= notify_threshold) {
        notify_bytes_available(bytes_available);
      }
    }
    if (len) {
      VERBOSE_PRINT(("WriteBuffer: Circular buffer full, sleeping...\n"));
      SwitchToThread();
    }
  }
  PROFILE_SEND("WriteBuffer");
}

static void LogAuxData(const PushBufferCommandTraceInfo *trigger,
                       AuxDataType type, const void *data, uint32_t len) {
  if (!trigger || !data || !len) {
    return;
  }

  AuxDataHeader header = {trigger->packet_index, type, len};
  WriteBuffer(state_machine.on_aux_buffer_bytes_available,
              &state_machine.aux_critical_section, state_machine.aux_buffer,
              &header, sizeof(header), 0);
  WriteBuffer(state_machine.on_aux_buffer_bytes_available,
              &state_machine.aux_critical_section, state_machine.aux_buffer,
              data, len, 0);
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

    PGRAPHCommandCallback pre_callback = NULL;
    PGRAPHCommandCallback post_callback = NULL;
    if (!skip_hooks) {
      GetMethodProcessors(method_info, &pre_callback, &post_callback);
    }

    PROFILE_INIT();

    if (pre_callback) {
      PROFILE_START();
      // Go where we can do pre-callback.
      RunFIFO(*dma_pull_addr);
      PROFILE_SEND("PreCallback - RunFIFO");

      // Do the pre callback before running the command
      // FIXME: assert we are where we wanted to be
      PROFILE_START();
      pre_callback(method_info, LogAuxData,
                   &state_machine.config.aux_tracing_config);
      PROFILE_SEND("PreCallback invocation:");
    }

    if (post_callback) {
      // If we reached target, we can't step again without leaving valid buffer

      if (*dma_pull_addr == state_machine.real_dma_push_addr) {
        DbgPrint("ERROR: Bad state in ProcessPushBufferCommand: 0x%X != 0x%X\n",
                 *dma_pull_addr, state_machine.real_dma_push_addr);
        return 0xFFFFFFFF;
      }

      // Go where we want to go (equivalent to step)
      PROFILE_START();
      RunFIFO(post_addr);
      PROFILE_SEND("PostCallback - RunFIFO");

      // We have processed all bytes now
      unprocessed_bytes = 0;

      PROFILE_START();
      post_callback(method_info, LogAuxData,
                    &state_machine.config.aux_tracing_config);
      PROFILE_SEND("PostCallback invocation");
    }
    //      // Add the pushbuffer command to log
    //      self._record_push_buffer_command(method_info, pre_info, post_info)
  }

  *dma_pull_addr = post_addr;

  return unprocessed_bytes;
}

static void LogCommand(const PushBufferCommandTraceInfo *info) {
  if (!info->valid) {
    return;
  }
  WriteBuffer(state_machine.on_pgraph_buffer_bytes_available,
              &state_machine.pgraph_critical_section,
              state_machine.pgraph_buffer, info, sizeof(*info),
              state_machine.pgraph_buffer_notify_threshold);
  if (info->data.data_state == PBCPDS_HEAP_BUFFER &&
      info->command.parameter_count) {
    uint32_t data_size = info->command.parameter_count * 4;
    WriteBuffer(state_machine.on_pgraph_buffer_bytes_available,
                &state_machine.pgraph_critical_section,
                state_machine.pgraph_buffer, info->data.data.heap_buffer,
                data_size, state_machine.pgraph_buffer_notify_threshold);
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
    info.subroutine_return_address = 0;
    uint32_t peek_unprocessed_bytes =
        ProcessPushBufferCommand(&peek_dma_pull_addr, &info, TRUE, TRUE);

    if (peek_unprocessed_bytes == 0xFFFFFFFF) {
      DbgPrint("ERROR: Failed to process pbuffer command during seek.\n");
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

  uint32_t command_index = 1;
  uint32_t last_push_addr = 0;
  uint32_t sleep_calls = 0;
  uint32_t stall_workarounds = 0;

#ifdef VERBOSE_DEBUG
  uint32_t commands_discarded = 0;
#endif

  PROFILE_INIT();

  while (TracerGetState() == working_state) {
    PushBufferCommandTraceInfo info;
    info.subroutine_return_address = 0;
    info.packet_index = command_index++;

    PROFILE_START();
    uint32_t unprocessed_bytes =
        ProcessPushBufferCommand(&dma_pull_addr, &info, discard, discard);
    PROFILE_SEND("ProcessPushBufferCommand");

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
        DbgPrint("WARNING: DMA Addresses invalid inside trace loop!\n");
        real_dma_push_addr = 0;
      }
    }

    BOOL is_flip = FALSE;
    BOOL is_empty = dma_pull_addr == real_dma_push_addr;

    if (info.valid && info.graphics_class == 0x97) {
      is_flip = info.command.method == NV097_FLIP_STALL;

      // The nxdk does not trigger a FLIP_STALL, but does do a FLIP_INC_WRITE.
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
        VERBOSE_PRINT(
            ("Tracer: Flushing buffer until (0x%08X): real_put 0x%X; "
             "bytes_queued: %d\n",
             dma_pull_addr, real_dma_push_addr, bytes_queued));
      } else {
        VERBOSE_PRINT(
            ("Tracer: No PGRAPH commands available. Real push: 0x%08X, live "
             "push: 0x%08X, live pull: 0x%08X\n",
             real_dma_push_addr, GetDMAPushAddress(), GetDMAPullAddress()));
      }

      PROFILE_START();
      RunFIFO(dma_pull_addr);
      PROFILE_SEND("Flush buffer - RunFIFO");
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
        if (++sleep_calls > 10) {
          sleep_calls = 0;
          if (++stall_workarounds > MAX_STALL_WORKAROUNDS) {
            DbgPrint("Permanent stall detected, aborting...\n");
            SetState(STATE_FATAL_PERMANENT_STALL);
            CompleteRequest();
            return;
          }
          DbgPrint("Stall detected, attempting to populate FIFO\n");
          PROFILE_START();
          // NOTE: EnableFIFO + ResumePusher + SwitchToThread() + PausePusher +
          // DisableFIFO is insufficient to fix this problem.
          EnablePGRAPHFIFO();
          ResumeFIFOPusher();
          ResumeFIFOPuller();
          Sleep(15);
          SwitchToThread();
          Sleep(15);
          PauseFIFOPusher();
          DisablePGRAPHFIFO();
          PROFILE_SEND("Stall workaround");
        }
      } else {
        last_push_addr = real_dma_push_addr;
        sleep_calls = 0;
      }

      VERBOSE_PRINT(
          ("Reached end of buffer with %d bytes queued - waiting 5 ms\n",
           bytes_queued));
      Sleep(5);
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
