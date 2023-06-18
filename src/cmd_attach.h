#ifndef NV2A_TRACE_CMD_ATTACH_H
#define NV2A_TRACE_CMD_ATTACH_H

#include "xbdm.h"

#define CMD_ATTACH "attach"

//! Creates a new tracer instance.
//!
//! \param command - The command string received from the remote.
//! \param response - Buffer into which an immediate response (e.g., an error
//! message) may be written. \param response_len - Maximum length of `response`.
//! \param ctx - Command context object that may be leveraged for a more
//! complicated multi-call response. \return XBOX specific HRESULT (e.g.,
//! XBOX_S_OK). See `xbdm_err.h`.
//!
//! Command sring parameters:
//!   psize - uint32 indicating the size in bytes to reserve for the pgraph
//!           circular buffer.
//!   gsize - uint32 indicating the size in bytes to reserve for the graphics
//!           circular buffer.
//!   tcap - uint32 boolean indicating whether texture captures should be
//!           performed.
//!   dcap - uint32 boolean indicating whether depth buffer captures should be
//!           performed.
//!   ccap - uint32 boolean indicating whether framebuffer captures should be
//!           performed.
//!   rdicap - uint32 boolean indicating whether RDI captures should be
//!           performed (this has significant performance impact).
HRESULT HandleAttach(const char *command, char *response, DWORD response_len,
                     CommandContext *ctx);

#endif  // NV2A_TRACE_CMD_ATTACH_H
