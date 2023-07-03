#include "tracer_state_machine.h"

#include "pushbuffer_command.h"
#include "register_defs.h"
#include "tracelib/exchange_dword.h"
#include "tracelib/kick_fifo.h"
#include "util/circular_buffer.h"
#include "xbdm.h"
#include "xbox_helper.h"

#define VERBOSE_DEBUG

typedef enum TracerRequest {
  REQ_NONE,
  REQ_WAIT_FOR_STABLE_PUSH_BUFFER,
  REQ_DISCARD_UNTIL_FLIP,
} TracerRequest;

typedef struct TracerStateMachine {
  HANDLE processor_thread;
  DWORD processor_thread_id;

  CRITICAL_SECTION state_critical_section;
  TracerState state;
  TracerRequest request;

  BOOL dma_addresses_valid;
  DWORD real_dma_pull_addr;
  DWORD real_dma_push_addr;
  DWORD target_dma_push_addr;

  NotifyStateChangedHandler on_notify_state_changed;

  TracerConfig config;
  CircularBuffer pgraph_buffer;
  CircularBuffer graphics_buffer;
} TracerStateMachine;

// Function signature for pre/post processing callbacks.
typedef void (*PGRAPHCommandCallback)(const PushBufferCommandTraceInfo *info);

typedef struct PGRAPHCommandProcessor {
  BOOL valid;

  // The command to be processed.
  DWORD command;

  PGRAPHCommandCallback *pre_callbacks;
  PGRAPHCommandCallback *post_callbacks;
} PGRAPHCommandProcessor;

typedef struct PGRAPHClassProcessor {
  DWORD class;
  PGRAPHCommandProcessor *processors;
} PGRAPHClassProcessor;

static const uint32_t kTag = 0x6E74534D;  // 'ntSM'

// Maximum number of bytes to leave in the FIFO before allowing it to be
// processed. A cap is necessary to prevent Direct3D from performing fixups that
// would not happen outside of tracing conditions.
static const DWORD kMaxQueueDepthBeforeFlush = 200;

static const PGRAPHClassProcessor kPGRAPHProcessorRegistry[] = {{0, NULL}};

static TracerStateMachine state_machine = {0};

static DWORD __attribute__((stdcall))
TracerThreadMain(LPVOID lpThreadParameter);

static void SetState(TracerState new_state);
static TracerRequest GetRequest(void);
static BOOL SetRequest(TracerRequest new_request);
static void Shutdown(void);

static void WaitForStablePushBufferState(void);
static void DiscardUntilFramebufferFlip(void);

static void *Allocator(size_t size) {
  return DmAllocatePoolWithTag(size, kTag);
}

static void Free(void *block) { return DmFreePool(block); }

HRESULT TracerInitialize(NotifyStateChangedHandler on_notify_state_changed) {
  if (!on_notify_state_changed) {
    DbgPrint("Invalid on_notify_state_changed handler.");
    return XBOX_E_FAIL;
  }
  if (state_machine.on_notify_state_changed) {
    DbgPrint("Tracer already initialized.");
    return XBOX_E_EXISTS;
  }

  state_machine.on_notify_state_changed = on_notify_state_changed;
  state_machine.state = STATE_UNINITIALIZED;
  InitializeCriticalSection(&state_machine.state_critical_section);

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
  if (state_machine.state > STATE_UNINITIALIZED) {
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

BOOL TracerGetDMAAddresses(DWORD *push_addr, DWORD *pull_addr) {
  EnterCriticalSection(&state_machine.state_critical_section);
  *push_addr = state_machine.real_dma_push_addr;
  *pull_addr = state_machine.real_dma_pull_addr;
  BOOL valid = TRUE;
  LeaveCriticalSection(&state_machine.state_critical_section);

  return valid;
}

static void NotifyStateChanged(TracerState new_state) {
  if (!state_machine.on_notify_state_changed) {
    return;
  }
  state_machine.on_notify_state_changed(new_state);
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
  if (state_machine.request != REQ_NONE &&
      state_machine.request != new_request) {
    ret = FALSE;
  } else {
    state_machine.request = new_request;
    ret = TRUE;
  }
  LeaveCriticalSection(&state_machine.state_critical_section);
  return ret;
}

static void SaveDMAAddresses(DWORD push_addr, DWORD pull_addr) {
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

HRESULT TracerBeginDiscardUntilFlip(void) {
  if (SetRequest(REQ_DISCARD_UNTIL_FLIP)) {
    return XBOX_S_OK;
  }
  return XBOX_E_ACCESS_DENIED;
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
        break;

      case REQ_DISCARD_UNTIL_FLIP:
        DiscardUntilFramebufferFlip();
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
}

static void WaitForStablePushBufferState(void) {
  TracerState current_state = TracerGetState();
  if (current_state == STATE_IDLE_STABLE_PUSH_BUFFER ||
      current_state == STATE_IDLE_NEW_FRAME) {
    NotifyStateChanged(current_state);
    return;
  }

  SetState(STATE_WAITING_FOR_STABLE_PUSH_BUFFER);

  DWORD dma_pull_addr = 0;
  DWORD dma_push_addr_real = 0;

  while (TracerGetState() == STATE_WAITING_FOR_STABLE_PUSH_BUFFER) {
    // Stop consuming CACHE entries.
    DisablePGRAPHFIFO();
    BusyWaitUntilPGRAPHIdle();

    // Kick the pusher so that it fills the CACHE.
    MaybePopulateFIFOCache();

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
    DWORD dma_push_addr_target = dma_pull_addr;
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

    DWORD dma_push_addr_check = GetDMAPushAddress();
    DWORD dma_pull_addr_check = GetDMAPullAddress();

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
    CompleteRequest();
    SetState(STATE_IDLE_STABLE_PUSH_BUFFER);
    return;
  }

  DbgPrint("Wait for idle aborted, restoring PFIFO state...\n");
  SetDMAPushAddress(dma_push_addr_real);
  EnablePGRAPHFIFO();
  ResumeFIFOPusher();

  SaveDMAAddresses(dma_push_addr_real, dma_pull_addr);
}

// Sets the DMA_PUSH_ADDR to the given target, storing the old value.
static void ExchangeDMAPushAddress(DWORD target) {
  DWORD prev_target = state_machine.target_dma_push_addr;
  //  DWORD prev_real = state_machine.real_dma_push_addr;

  DWORD real = ExchangeDWORD(DMA_PUSH_ADDR, target);
  state_machine.target_dma_push_addr = target;

  // It must point where we pointed previously, otherwise something is broken.
  if (real != prev_target) {
    //    self.html_log.print_log(
    //        "New real PUT (0x%08X -> 0x%08X) while changing hook 0x%08X ->
    //        0x%08X" % (prev_real, real, prev_target, target)
    //    )
    DWORD push_state = ReadDWORD(CACHE_PUSH_STATE);
    if (push_state & 0x01) {
      DbgPrint("PUT was modified and pusher was already active!\n");
      Sleep(60 * 1000);
    }

    state_machine.real_dma_push_addr = real;
  }
}

// Runs the PFIFO until the DMA_PULL_ADDR equals the given address.
static void RunFIFO(DWORD pull_addr_target) {
  // Mark the pushbuffer as empty by setting the push address to the target pull
  // address.

  ExchangeDMAPushAddress(pull_addr_target);
  // FIXME: we can avoid this read in some cases, as we should know where we are
  state_machine.real_dma_pull_addr = GetDMAPullAddress();
  //  self.html_log.log(
  //      [
  //          "WARNING",
  //          "Running FIFO (GET: 0x%08X -- PUT: 0x%08X / 0x%08X)"
  //          % (self.real_dma_pull_addr, pull_addr_target,
  //          self.real_dma_push_addr),
  //      ]
  //  )

  // Loop while this command is being run.
  // This is necessary because a whole command might not fit into CACHE.
  // So we have to process it chunk by chunk.
  // FIXME: This used to be a check which made sure that `dma_pull_addr` did
  //       never leave the known PB.
  DWORD iterations_with_no_change = 0;
  while (state_machine.real_dma_pull_addr != pull_addr_target) {
    if (iterations_with_no_change && !(iterations_with_no_change % 1000)) {
      DbgPrint(
          "Warning: %d iterations with no change to DMA_PULL_ADDR 0x%X "
          " target 0x%X\n",
          iterations_with_no_change, state_machine.real_dma_pull_addr,
          pull_addr_target);
    }

#ifdef VERBOSE_DEBUG
    DbgPrint(
        "RunFIFO: At 0x%08X, target is 0x%08X (Real: 0x%08X)\n"
        "         PULL ADDR: 0x%X  PUSH: 0x%X\n",
        state_machine.real_dma_pull_addr, pull_addr_target,
        state_machine.real_dma_push_addr, GetDMAPullAddress(),
        GetDMAPushAddress());
#endif

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
    DWORD new_get_addr = GetDMAPullAddress();
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

static DWORD ProcessPushBufferCommand(DWORD *dma_pull_addr,
                                      PushBufferCommandTraceInfo *method_info,
                                      BOOL discard, BOOL skip_hooks) {
  method_info->valid = FALSE;
  DWORD unprocessed_bytes = 0;

  /*
    self.html_log.log(
        [
            "WARNING",
            "Starting FIFO parsing from 0x%08X -- 0x%08X"
            % (pull_addr, self.real_dma_push_addr),
        ]
    )
   */

  if (*dma_pull_addr == state_machine.real_dma_push_addr) {
    //    self.html_log.log(
    //      [
    //        "WARNING",
    //        "Sucessfully finished FIFO parsing 0x%08X -- 0x%08X (%d bytes
    //        unprocessed)" % (pull_addr, self.real_dma_push_addr,
    //        unprocessed_bytes),
    //      ]
    //    )
    return 0;
  }

  // Filter command and check where it wants to go to.
  DWORD post_addr =
      ParsePushBufferCommandTraceInfo(*dma_pull_addr, method_info, discard);
  if (!post_addr) {
    DeletePushBufferCommandTraceInfo(method_info);
    return 0xFFFFFFFF;
  }

  if (!method_info->valid) {
    DbgPrint("WARNING: No method. Going to 0x%08X", post_addr);
    unprocessed_bytes = 4;
  } else {
    // Mark the size of the instruction + any associated parameters.
    unprocessed_bytes = 4 + method_info->command.method_count * 4;

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

  /*
    self.html_log.log(
        [
            "WARNING",
            "Sucessfully finished FIFO parsing 0x%08X -- 0x%08X (%d bytes
    unprocessed)" % (pull_addr, self.real_dma_push_addr, unprocessed_bytes),
        ]
    )
  */

  return unprocessed_bytes;
}

static void DiscardUntilFramebufferFlip(void) {
  TracerState current_state = TracerGetState();
  if (current_state == STATE_IDLE_NEW_FRAME) {
    NotifyStateChanged(current_state);
    return;
  }

  if (!state_machine.dma_addresses_valid) {
    SetState(STATE_FATAL_NOT_IN_STABLE_STATE);
    return;
  }

  SetState(STATE_DISCARDING_UNTIL_FLIP);

  DWORD bytes_queued = 0;
  DWORD dma_pull_addr = state_machine.real_dma_pull_addr;
  DWORD commands_discarded = 0;

  while (TracerGetState() == STATE_DISCARDING_UNTIL_FLIP) {
    PushBufferCommandTraceInfo info;
    DWORD unprocessed_bytes =
        ProcessPushBufferCommand(&dma_pull_addr, &info, TRUE, TRUE);
    if (unprocessed_bytes == 0xFFFFFFFF) {
      SetState(STATE_FATAL_PROCESS_PUSH_BUFFER_COMMAND_FAILED);
      return;
    }
    bytes_queued += unprocessed_bytes;

    BOOL is_flip = info.valid && info.graphics_class == 0x97 &&
                   (info.command.method == NV097_FLIP_STALL ||
                    info.command.method == NV097_FLIP_INCREMENT_WRITE);
    BOOL is_empty = dma_pull_addr == state_machine.real_dma_push_addr;

#ifdef VERBOSE_DEBUG
    if (info.valid & !is_flip && info.graphics_class == 0x97) {
      DbgPrint("Discarding command 0x%X - 0x%X\n", info.graphics_class,
               info.command.method);
    }
#endif  // VERBOSE_DEBUG

    // Avoid queuing up too many bytes: while the buffer is being processed,
    // D3D might fixup the buffer if GET is still too far away.
    if (is_empty || is_flip || bytes_queued >= kMaxQueueDepthBeforeFlush) {
      DbgPrint(
          "Flushing buffer until (0x%08X): real_put 0x%X; bytes_queued: %d\n",
          dma_pull_addr, state_machine.real_dma_push_addr, bytes_queued);
      RunFIFO(dma_pull_addr);
      bytes_queued = 0;
    }

    if (is_empty) {
      DbgPrint("Reached end of buffer with %d bytes queued?!\n", bytes_queued);
      Sleep(1);
      //      state_machine.xbox_helper.print_enable_states()
      //      state_machine.xbox_helper.print_pb_state()
      //      state_machine.xbox_helper.print_dma_state()
      //      state_machine.xbox_helper.print_cache_state()
    }

    // Verify we are where we think we are
    if (!bytes_queued) {
      DWORD dma_pull_addr_real = GetDMAPullAddress();
      if (dma_pull_addr_real != dma_pull_addr) {
        DbgPrint(
            "ERROR: Corrupt state. HW (0x%08X) is not at parser (0x%08X)\n",
            dma_pull_addr_real, dma_pull_addr);
        SetState(STATE_FATAL_DISCARDING_FAILED);
        return;
      }
    }

    if (is_flip) {
      CompleteRequest();
      SetState(STATE_IDLE_NEW_FRAME);
      return;
    }

    if (is_empty && !bytes_queued) {
      Sleep(0);
    }
#ifdef VERBOSE_DEBUG
    if (!(++commands_discarded & 0x01FF)) {
      DbgPrint(
          "Awaiting flip stall... Discarded %d commands   PULL@ 0x%X  "
          "REAL_PUSH: 0x%X  BQ: %d  MTHD: 0x%X\n",
          commands_discarded, dma_pull_addr, state_machine.real_dma_push_addr,
          bytes_queued, info.command.method);
    }
#endif
  }
}
