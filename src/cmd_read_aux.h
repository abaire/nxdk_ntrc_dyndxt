#ifndef NTRC_DYNDXT_SRC_CMD_READ_AUX_H_
#define NTRC_DYNDXT_SRC_CMD_READ_AUX_H_

#include "xbdm.h"

#define CMD_READ_AUX "read_aux"

//! Reads data from the auxiliary data buffer.
//!
//! The response will be a size-prefixed binary (the first 4 bytes indicate the
//! size, followed by data).
//!
//! \param command - The command string received from the remote.
//! \param response - Buffer into which an immediate response (e.g., an error
//! message) may be written.
//! \param response_len - Maximum length of `response`.
//! \param ctx - Command context object that may be leveraged for a more
//! complicated multi-call response.
//!
//! \return XBOX specific HRESULT (e.g., XBOX_S_OK). See `xbdm_err.h`.
//!
//! Command string parameters:
//!   maxsize - uint32 indicating the maximum size in bytes to read.
HRESULT HandleReadAux(const char *command, char *response,
                      uint32_t response_len, CommandContext *ctx);

#endif  // NTRC_DYNDXT_SRC_CMD_READ_AUX_H_
