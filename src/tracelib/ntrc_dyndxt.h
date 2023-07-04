//! Shared constants.
#ifndef NTRC_DYNDXT_SRC_NTRC_DYNDXT_H_
#define NTRC_DYNDXT_SRC_NTRC_DYNDXT_H_

//! Name of the installed xbdm message handler. Also used as the prefix for
//! XBOX -> debugger push notifications.
#define NTRC_HANDLER_NAME "ntrc"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TracerState {
  STATE_FATAL_NOT_IN_NEW_FRAME_STATE = -1012,
  STATE_FATAL_NOT_IN_STABLE_STATE = -1011,
  STATE_FATAL_DISCARDING_FAILED = -1010,
  STATE_FATAL_PROCESS_PUSH_BUFFER_COMMAND_FAILED = -1000,

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

  STATE_TRACING_UNTIL_FLIP = 1020,
} TracerState;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NTRC_DYNDXT_SRC_NTRC_DYNDXT_H_
