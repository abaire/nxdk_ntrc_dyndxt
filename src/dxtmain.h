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
