#ifndef NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_
#define NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_

#include <windows.h>

typedef struct PushBufferCommand {
  // Whether the data contained in this struct is valid or not.
  BOOL valid;

  // Processing this command should not automatically increment the target
  // address.
  BOOL non_increasing;

  // The ID of the method. E.g., `NV097_FLIP_STALL`.
  DWORD method;

  // The subchannel of the method.
  DWORD subchannel;

  // The number of parameters to the method.
  DWORD method_count;
} PushBufferCommand;

typedef struct PushBufferCommandTraceInfo {
  // Whether the data contained in this struct is valid or not.
  BOOL valid;
  PushBufferCommand command;

  DWORD address;
  DWORD graphics_class;

  // Parameters passed to the command, if any.
  // If populated, this will always be exactly (command.method_count * 4) bytes.
  uint8_t *data;
} PushBufferCommandTraceInfo;

// Processes the given `command` DWORD.
// Populates the given `PushBufferCommand` with expanded details of the command.
// If the command is not processable, sets `info->valid` to FALSE;
//
// Returns a DWORD indicating the next command address, based on `addr` or 0 to
// indicate a critical error.
DWORD ParsePushBufferCommand(DWORD addr, DWORD command,
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
// Returns a DWORD indicating the next command address after `pull_addr` or 0
// to indicate a critical error.
DWORD ParsePushBufferCommandTraceInfo(DWORD pull_addr,
                                      PushBufferCommandTraceInfo *info,
                                      BOOL discard_parameters);

void DeletePushBufferCommandTraceInfo(PushBufferCommandTraceInfo *info);

#endif  // NXDK_NTRC_DYNDXT_PUSHBUFFER_COMMAND_H_
