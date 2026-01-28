#ifndef NTRC_DYNDXT_SRC_XBDM_UTIL_H_
#define NTRC_DYNDXT_SRC_XBDM_UTIL_H_

#include "xbdm.h"

typedef struct SendPrepopulatedBinaryDataContext {
  //! The data to send.
  void *buffer;

  //! The offset into `buffer` from which the next valid byte should be copied.
  //! Must be initialized to zero.
  uint32_t read_offset;

  //! Whether or not to DmFreePool `buffer` after sending the last byte.
  BOOL free_buffer_on_complete;

  //! Whether or not to DmFreePool `this` after sending the last byte.
  BOOL free_self_on_complete;
} SendPrepopulatedBinaryDataContext;

//! Initializes the given CommandContext and SendPrepopulatedBinaryDataContext
//! for a binary data transfer.
void InitializeSendPrepopulatedBinaryDataContexts(
    CommandContext *ctx, SendPrepopulatedBinaryDataContext *send_context,
    void *buffer, uint32_t buffer_size, BOOL free_buffer_on_complete,
    BOOL free_context_on_complete);

#endif  // NTRC_DYNDXT_SRC_XBDM_UTIL_H_
