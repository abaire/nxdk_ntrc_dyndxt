// Main entrypoint for the NV2A tracer DynamicDXT.
//
// This DXT exposes a number of XBDM methods to allow interaction with the
// tracer, see the `kCommandTableDef` table in dxtmain.c.
//
// This DXT also sends a number of notifications:
//
//   new_state=<state_number> - Notifies of a state change in the tracer state
//       machine. See the `TracerState` enum.
//   w_pgraph=<new_bytes_writtem> - Notifies that bytes have been written to the
//       PGRAPH trace buffer and may be retrieved via a `read_pgraph` call. It
//       is important to perform a read to avoid having the buffer fill up,
//       blocking tracing.
//   w_graphics=<new_bytes_writtem> - Notifies that bytes have been written to
//       the graphics trace buffer and may be retrieved via a `read_graphics`
//       call. It is important to perform a read to avoid having the buffer fill
//       up, blocking tracing.
#ifndef NV2A_TRACE_DXTMAIN_H
#define NV2A_TRACE_DXTMAIN_H

#include <windows.h>

#include "xbdm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CommandTableEntry {
  const char *command;
  HRESULT (*processor)(const char *, char *, DWORD, CommandContext *);
} CommandTableEntry;

extern const CommandTableEntry *kCommandTable;
extern const uint32_t kCommandTableNumEntries;

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // NV2A_TRACE_DXTMAIN_H
