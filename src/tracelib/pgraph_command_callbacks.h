#ifndef NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_
#define NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_

#include <stdint.h>

#include "pushbuffer_command.h"

#ifdef __cplusplus
extern "C" {
#endif

//! Describes some auxiliary buffer data type.
typedef enum AuxDataType {
  //! A raw dump of the PGRAPH region.
  ADT_PGRAPH_DUMP,
  //! A raw dump of the PFB region.
  ADT_PFB_DUMP,
  //! A raw dump of the RDI data.
  ADT_RDI_DUMP,
  //! A surface buffer of some sort.
  ADT_SURFACE,
  //! A texture.
  ADT_TEXTURE,
} AuxDataType;

//! Header describing an entry in the auxiliary data stream.
typedef struct AuxDataHeader {
  //! The index of the PushBufferCommandTraceInfo packet with which this data is
  //! associated.
  uint32_t packet_index;

  //! A value from AuxDataType indicating the type of data.
  uint32_t data_type;

  //! The length of the data, which starts immediately following this header.
  uint32_t len;
} __attribute((packed)) AuxDataHeader;

//! Header describing RDI data.
typedef struct RDIHeader {
  //! The offset from which the following RDI values were read.
  uint32_t offset;
  //! The number of 32-bit values that follow this struct.
  uint32_t count;
} __attribute((packed)) RDIHeader;

//! Describes the application of a surface.
typedef enum SurfaceType {
  ST_COLOR,
  ST_DEPTH,
} SurfaceType;

//! Header describing surface data.
typedef struct SurfaceHeader {
  //! The intended use of this surface.
  uint32_t type;
  //! The number of ASCII characters immediately following this header
  //! containing a description of the content.
  uint32_t description_len;
  //! The number of image bytes immediately following the description
  //! characters.
  uint32_t len;

  uint32_t width;
  uint32_t height;
  uint32_t pitch;
} __attribute((packed)) SurfaceHeader;

//! Header describing texture data.
typedef struct TextureHeader {
  //! The texture unit/stage that this texture is associated with.
  uint32_t stage;

  //! The layer index of this texture.
  uint32_t layer;

  //! The number of image bytes immediately following this header.
  uint32_t len;

  uint32_t format;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t pitch;
  //! The value of the control0 register.
  uint32_t control0;
  //! The value of the control1 register.
  uint32_t control1;
} __attribute((packed)) TextureHeader;

//! Controls auxiliary buffer tracing.
typedef struct AuxConfig {
  //! Enables capture of the PGRAPH region.
  BOOL raw_pgraph_capture_enabled;

  //! Enables capture of the PFB region.
  BOOL raw_pfb_capture_enabled;

  //! Enables capture of RDI state.
  BOOL rdi_capture_enabled;

  //! Enables capture of color surfaces.
  BOOL surface_color_capture_enabled;

  //! Enables capture of depth surfaces.
  BOOL surface_depth_capture_enabled;

  //! Enables capture of texture stage sources.
  BOOL texture_capture_enabled;
} AuxConfig;

//! Callback that may be invoked to send auxiliary data to the remote.
//!
//! \param trigger - The PushBufferCommandTraceInfo that this data is associated
//!                  with.
//! \param type - The type of the buffer.
//! \param data - The data to send.
//! \param len - The length of `data`.
typedef void (*StoreAuxData)(const PushBufferCommandTraceInfo *trigger,
                             AuxDataType type, const void *data, uint32_t len);

//! Dump color/depth surfaces, shader data, etc...
void TraceSurfaces(const PushBufferCommandTraceInfo *info, StoreAuxData store,
                   const AuxConfig *config);

//! Dump textures.
void TraceBegin(const PushBufferCommandTraceInfo *info, StoreAuxData store,
                const AuxConfig *config);

//! Dump surfaces.
void TraceEnd(const PushBufferCommandTraceInfo *info, StoreAuxData store,
              const AuxConfig *config);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NTRC_DYNDXT_SRC_TRACELIB_PGRAPH_COMMAND_CALLBACKS_H_
