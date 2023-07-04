#ifndef NXDK_NTRC_DYNDXT_CMD_DISCARD_UNTIL_FLIP_H_
#define NXDK_NTRC_DYNDXT_CMD_DISCARD_UNTIL_FLIP_H_

#include "xbdm.h"

#define CMD_DISCARD_UNTIL_FLIP "discard_until_flip"

//! Steps through pgraph commands, discarding them until the next frame flip,
//! then returns to idle state.
//!
//! \param command - The command string received from the remote.
//! \param response - Buffer into which an immediate response (e.g., an error
//! message) may be written. \param response_len - Maximum length of `response`.
//! \param ctx - Command context object that may be leveraged for a more
//! complicated multi-call response. \return XBOX specific HRESULT (e.g.,
//! XBOX_S_OK). See `xbdm_err.h`.
//!
//! Command string parameters:
//!   require_flip - Optional key indicating that the current frame must be
//!       discarded, even if execution is paused at the start of the frame.
HRESULT HandleDiscardUntilFlip(const char *command, char *response,
                               uint32_t response_len, CommandContext *ctx);

#endif  // NXDK_NTRC_DYNDXT_CMD_DISCARD_UNTIL_FLIP_H_
