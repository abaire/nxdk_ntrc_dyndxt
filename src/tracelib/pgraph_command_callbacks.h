#ifndef NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_
#define NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_

#include <stdint.h>

#include "pushbuffer_command.h"

#ifdef __cplusplus
extern "C" {
#endif

//! Describes some auxilliary buffer data type.
typedef enum AuxDataType {
  //! A color buffer.
  ADT_FRAMEBUFFER,
} AuxDataType;

//! Header describing an entry in the auxilliary data stream.
typedef struct AuxDataHeader {
  //! The index of the PushBufferCommandTraceInfo packet with which this data is
  //! associated.
  uint32_t packet_index;

  //! A value from AuxDataType indicating the type of data.
  uint32_t data_type;

  //! The length of the data, which starts immediately following this header.
  uint32_t len;
} __attribute((packed)) AuxDataHeader;

//! Callback that may be invoked to send auxilliary data to the remote.
//!
//! \param trigger - The PushBufferCommandTraceInfo that this data is associated
//!                  with.
//! \param type - The type of the buffer.
//! \param data - The data to send.
//! \param len - The length of `data`.
typedef void (*StoreAuxData)(const PushBufferCommandTraceInfo *trigger,
                             AuxDataType type, const void *data, uint32_t len);

void TraceSurfaces(const PushBufferCommandTraceInfo *info, StoreAuxData store);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_
