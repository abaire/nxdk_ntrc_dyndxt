#ifndef NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_
#define NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

//! Provides details about a PGRAPH command.
typedef struct PushBufferCommand {
  //! Whether the data contained in this struct is valid or not. Command structs
  //! may be invalid due to an error or because the command was a jump.
  uint32_t valid;

  //! Whether processing this command should automatically increment the target
  //! address.
  uint32_t non_increasing;

  //! The ID of the method. E.g., `NV097_FLIP_STALL`.
  uint32_t method;

  //! The subchannel of the method.
  uint32_t subchannel;

  //! The number of parameters to the method.
  uint32_t parameter_count;
} __attribute((packed)) PushBufferCommand;

//! Encapsulates information about a single PGRAPH command.
typedef struct PushBufferCommandTraceInfo {
  //! Whether the data contained in this struct is valid or not.
  uint32_t valid;

  //! The arbitrary packet index, used to match the packet with associated
  //! captures (e.g., framebuffer dumps).
  uint32_t packet_index;

  //! The actual command.
  PushBufferCommand command;

  //! The address from which this packet was read.
  uint32_t address;

  //! The PGRAPH graphics class for this packet (e.g., 0x97 for 3D).
  uint32_t graphics_class;

  // Parameters passed to the command, if any.
  // If populated, this will always be exactly (command.parameter_count * 4)
  // bytes.
  uint8_t *data;
} __attribute((packed)) PushBufferCommandTraceInfo;

//! Processes the given `command` uint32_t, populating a `PushBufferCommand`
//! with expanded details.
//!
//! On fatal error, returns 0.
//! If the command is processed in some way, returns the address of the next
//! command.
//! The `valid` field inside of the `PushBufferCommand` indicates whether the
//! other fields contain interesting data. Not all valid commands produce valid
//! `PushBufferCommand` data, so it is important to check both the return of
//! this method and the `valid` field.
uint32_t ParsePushBufferCommand(uint32_t addr, uint32_t command,
                                PushBufferCommand *info);

// Processes a pushbuffer command starting at the given address.
// Populates the given `PushBufferCommandTraceInfo` with the expanded details of
// the command. If the command is not processable, sets `info->valid` to FALSE;
//
// If `discard_parameters` is FALSE, copies any parameters to the method into
// a newly allocated buffer in `info->data`. The caller is responsible for
// freeing the buffer by calling `DeletePushBufferCommandTraceInfo`.
//
// If `discard_parameters` is TRUE, or the command has no parameters,
// `info->data` will be set to NULL.
//
// Returns a uint32_t indicating the next command address after `pull_addr` or 0
// to indicate a critical error.
uint32_t ParsePushBufferCommandTraceInfo(uint32_t pull_addr,
                                         PushBufferCommandTraceInfo *info,
                                         BOOL discard_parameters);

void DeletePushBufferCommandTraceInfo(PushBufferCommandTraceInfo *info);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_
