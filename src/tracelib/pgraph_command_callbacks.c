#include "pgraph_command_callbacks.h"

#include <stdio.h>
#include <string.h>

#include "pushbuffer_command.h"
#include "register_defs.h"
#include "xbdm.h"
#include "xbox_helper.h"
#include "xemu/hw/xbox/nv2a/nv2a_regs.h"

#define VERBOSE_DEBUG

#ifdef VERBOSE_DEBUG
#define VERBOSE_PRINT(c) DbgPrint c
#else
#define VERBOSE_PRINT(c)
#endif

static const uint32_t kTag = 0x6E744343;  // 'ntCC'

//! Value that may be added to contiguous memory addresses to access as
//! ADDR_AGPMEM, which is guaranteed to be linear (and thus may be slower than
//! tiled ADDR_FBMEM but can be manipulated directly).
static const uint32_t kAGPMemoryBase = 0xF0000000;

typedef struct TextureParameters {
  uint32_t width;
  uint32_t height;
  uint32_t color_pitch;
  uint32_t color_offset;
  uint32_t format_color;
  uint32_t format_depth;
  uint32_t depth_pitch;
  uint32_t depth_offset;
  uint32_t surface_type;
  BOOL swizzled;
} TextureParameters;

static void ApplyAntiAliasingFactor(uint32_t antialiasing_mode, uint32_t *x,
                                    uint32_t *y) {
  switch (antialiasing_mode) {
    case 0:
      return;

    case 1:
      *x = (*x * 2);
      return;

    case 2:
      *x = (*x * 2);
      *y = (*y * 2);
      return;

    default:
      DbgPrint("Invalid antialiasing mode %d\n", antialiasing_mode);
  }
}

static void ReadTextureParameters(TextureParameters *params) {
  params->color_pitch = ReadDWORD(0xFD400858);
  params->depth_pitch = ReadDWORD(0xFD40085C);
  params->color_offset = ReadDWORD(0xFD400828);
  params->depth_offset = ReadDWORD(0xFD40082C);

  uint32_t color_base = ReadDWORD(0xFD400840);
  uint32_t depth_base = ReadDWORD(0xFD400844);

  // FIXME: Is this correct? pbkit uses _base, but D3D seems to use _offset?
  params->color_offset += color_base;
  params->depth_offset += depth_base;

  uint32_t surface_clip_x = ReadDWORD(0xFD4019B4);
  uint32_t surface_clip_y = ReadDWORD(0xFD4019B8);

  params->surface_type = ReadDWORD(0xFD400710);

  //  uint32_t swizzle_unk = ReadDWORD(0xFD400818);
  //  uint32_t swizzle_unk2 = ReadDWORD(0xFD40086C);

  uint32_t clip_x = surface_clip_x & 0xFFFF;
  uint32_t clip_y = surface_clip_y & 0xFFFF;

  uint32_t clip_w = (surface_clip_x >> 16) & 0xFFFF;
  uint32_t clip_h = (surface_clip_y >> 16) & 0xFFFF;

  BOOL surface_anti_aliasing = ((params->surface_type >> 4) & 3) != 0;

  ApplyAntiAliasingFactor(surface_anti_aliasing, &clip_x, &clip_y);
  ApplyAntiAliasingFactor(surface_anti_aliasing, &clip_w, &clip_h);

  params->width = clip_x + clip_w;
  params->height = clip_y + clip_h;

  // FIXME: 128 x 128 [pitch = 256 (0x100)], at 0x01AA8000 [PGRAPH:0x01AA8000?],
  //   format 0x5, type: 0x21000002, swizzle: 0x7070000 [used 0]

  // FIXME: This does not seem to be a good field for this
  // FIXME: Patched to give 50% of coolness
  params->swizzled = (params->surface_type & 3) == 2;

  // FIXME: if surface_type is 0, we probably can't even draw..
  uint32_t draw_format = ReadDWORD(0xFD400804);
  params->format_color = (draw_format >> 12) & 0xF;

  // FIXME: Support 3D surfaces.
  params->format_depth = (draw_format >> 18) & 0x3;

  // TODO: Extract swizzle and float state.
}

//! Stores the PGRAPH region.
static void StorePGRAPH(const PushBufferCommandTraceInfo *info,
                        StoreAuxData store) {
#define PGRAPH_REGION 0xFD400000
#define PGRAPH_REGION_SIZE 0x2000
  uint8_t *buffer = (uint8_t *)DmAllocatePoolWithTag(PGRAPH_REGION_SIZE, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading PGRAPH region.");
    return;
  }

  // 0xFD400200 hangs Xbox, but skipping 0x200 - 0x400 works.
  // TODO: Needs further testing which regions work.
  uint8_t *write_ptr = buffer;
  memcpy(write_ptr, (uint8_t *)PGRAPH_REGION, 0x200);
  write_ptr += 0x200;

  // Null out the unreadable bytes.
  memset(write_ptr, 0, 0x200);
  write_ptr += 0x200;

  memcpy(write_ptr, (uint8_t *)(PGRAPH_REGION + 0x400),
         PGRAPH_REGION_SIZE - 0x400);

  store(info, ADT_PGRAPH_DUMP, buffer, 0x2000);
  DmFreePool(buffer);
}

//! Stores the PFB region.
static void StorePFB(const PushBufferCommandTraceInfo *info,
                     StoreAuxData store) {
#define PFB_REGION 0xFD100000
#define PFB_REGION_SIZE 0x1000
  uint8_t *buffer = (uint8_t *)DmAllocatePoolWithTag(PFB_REGION_SIZE, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading PFB region.");
    return;
  }

  memcpy(buffer, (uint8_t *)PFB_REGION, PFB_REGION_SIZE);

  store(info, ADT_PFB_DUMP, buffer, 0x2000);
  DmFreePool(buffer);
}

#define NV10_PGRAPH_RDI_INDEX 0xFD400750
#define NV10_PGRAPH_RDI_DATA 0xFD400754

//! Stores RDI data.
static void StoreRDI(const PushBufferCommandTraceInfo *info, StoreAuxData store,
                     uint32_t offset, uint32_t count) {
  uint32_t buffer_size = sizeof(RDIHeader) + 4 * count;
  uint8_t *buffer = (uint8_t *)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint(
        "Error: Failed to allocate buffer when reading %u RDI values from "
        "offset 0x%X.",
        count, offset);
    return;
  }

  RDIHeader *header = (RDIHeader *)buffer;
  header->offset = offset;
  header->count = count;
  uint32_t *write_ptr = (uint32_t *)(buffer + sizeof(*header));

  // FIXME: Assert pusher access is disabled
  // FIXME: Assert PGRAPH idle

  // TODO: Confirm behavior:
  // It may be that reading the DATA register 4 times returns X,Y,Z,W (not
  // necessarily in that order), but during that time the INDEX register
  // will stay constant, only being incremented on the final read.

  WriteDWORD(NV10_PGRAPH_RDI_INDEX, offset);

  // It is not safe and likely incorrect to do a bulk read so this must be
  // done individually.
  for (uint32_t i = 0; i < count; ++i) {
    *write_ptr++ = ReadDWORD(NV10_PGRAPH_RDI_DATA);
  }

  // FIXME: Restore original RDI?
  // Note: It may not be possible to restore the original index.
  // If you touch the INDEX register, you may or may not be resetting the
  // internal state machine.
  //
  // FIXME: Assert the conditions from entry have not changed

  store(info, ADT_RDI_DUMP, buffer, buffer_size);
  DmFreePool(buffer);
}

static void StoreSurface(const PushBufferCommandTraceInfo *info,
                         StoreAuxData store, SurfaceType type,
                         uint32_t surface_offset, uint32_t width,
                         uint32_t height, uint32_t pitch,
                         const char *description) {
  uint32_t len = pitch * height;
  if (!len) {
    DbgPrint(
        "Error: calculated zero length when reading surface %d. W=%u H=%u P=%u",
        type, width, height, pitch);
    return;
  }
  uint32_t description_len = strlen(description);
  uint32_t buffer_size = sizeof(SurfaceHeader) + description_len + len;

  uint8_t *buffer = (uint8_t *)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading surface %d.", type);
    return;
  }

  SurfaceHeader *header = (SurfaceHeader *)buffer;
  header->type = type;
  header->len = len;
  header->width = width;
  header->height = height;
  header->pitch = pitch;
  header->description_len = description_len;

  uint8_t *write_ptr = buffer + sizeof(*header);
  // null terminator is intentionally omitted.
  memcpy(write_ptr, description, description_len);  // NOLINT
  write_ptr += description_len;

  memcpy(write_ptr, (const uint8_t *)(kAGPMemoryBase | surface_offset), len);

  store(info, ADT_SURFACE, buffer, buffer_size);
  DmFreePool(buffer);
}

void TraceSurfaces(const PushBufferCommandTraceInfo *info, StoreAuxData store,
                   const AuxConfig *config) {
  if (config->raw_pgraph_capture_enabled) {
    StorePGRAPH(info, store);
  }

  if (config->raw_pfb_capture_enabled) {
    StorePFB(info, store);
  }

  if (!config->surface_color_capture_enabled &&
      !config->surface_depth_capture_enabled) {
    return;
  }

  TextureParameters params;
  ReadTextureParameters(&params);

  if (!params.format_color) {
    DbgPrint("Warning: Invalid color format, skipping surface dump.");
    return;
  }

  if (config->surface_color_capture_enabled && params.color_offset) {
    char description[256];
    snprintf(description, sizeof(description),
             "%d x %d [pitch = %d (0x%X)], at 0x%08X, format 0x%X, type: 0x%X, "
             "swizzled: %s",
             params.width, params.height, params.color_pitch,
             params.color_pitch, params.color_offset, params.format_color,
             params.surface_type, params.swizzled ? "Y" : "N");
    StoreSurface(info, store, ST_COLOR, params.color_offset, params.width,
                 params.height, params.color_pitch, description);
  }
  if (config->surface_depth_capture_enabled && params.depth_offset) {
    char description[256];
    snprintf(description, sizeof(description),
             "%d x %d [pitch = %d (0x%X)], at 0x%08X, format 0x%X, type: 0x%X, "
             "swizzled: %s",
             params.width, params.height, params.depth_pitch,
             params.depth_pitch, params.depth_offset, params.format_depth,
             params.surface_type, params.swizzled ? "Y" : "N");
    StoreSurface(info, store, ST_DEPTH, params.depth_offset, params.width,
                 params.height, params.depth_pitch, description);
  }

  if (config->rdi_capture_enabled) {
    // Vertex shader instructions.
    StoreRDI(info, store, 0x100000, 136 * 4);

    // Vertex shader constants 0 (192 four-element vectors).
    StoreRDI(info, store, 0x170000, 192 * 4);

    // Vertex shader constants 1 (192 four-element vectors).
    StoreRDI(info, store, 0xCC0000, 192 * 4);
  }
}

static void StoreTextureLayer(const PushBufferCommandTraceInfo *info,
                              StoreAuxData store, uint32_t stage,
                              uint32_t layer, uint32_t adjusted_offset,
                              uint32_t width, uint32_t height, uint32_t depth,
                              uint32_t pitch, uint32_t format,
                              uint32_t control0, uint32_t control1) {
  uint32_t len = pitch * height;
  if (!len) {
    DbgPrint(
        "Error: calculated zero length when reading texture %u:%u. W=%u H=%u "
        "P=%u",
        stage, layer, width, height, pitch);
    return;
  }
  uint32_t buffer_size = sizeof(TextureHeader) + len;

  uint8_t *buffer = (uint8_t *)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading texture %u:%u.",
             stage, layer);
    return;
  }

  TextureHeader *header = (TextureHeader *)buffer;
  header->stage = stage;
  header->layer = layer;

  header->len = len;
  header->format = format;
  header->width = width;
  header->height = height;
  header->depth = depth;
  header->pitch = pitch;
  header->control0 = control0;
  header->control1 = control1;

  uint8_t *write_ptr = buffer + sizeof(*header);
  memcpy(write_ptr, (const uint8_t *)(kAGPMemoryBase | adjusted_offset), len);

  store(info, ADT_TEXTURE, buffer, buffer_size);
  DmFreePool(buffer);
}

static void StoreTextureStage(const PushBufferCommandTraceInfo *info,
                              StoreAuxData store, uint32_t stage) {
  // Verify that the stage is enabled.
  uint32_t reg_offset = stage * 4;
  uint32_t control0 = ReadDWORD(PGRAPH_TEXCTL0_0 + reg_offset);
  if (!(control0 & (1 << 30))) {
    return;
  }

  uint32_t offset = ReadDWORD(PGRAPH_TEXOFFSET0 + reg_offset);
  uint32_t control1 = ReadDWORD(PGRAPH_TEXCTL1_0 + reg_offset);
  uint32_t pitch = control1 >> 16;
  uint32_t format = ReadDWORD(PGRAPH_TEXFMT0 + reg_offset);

  uint32_t fmt_color = (format >> 8) & 0x7F;
  uint32_t width_shift = (format >> 20) & 0xF;
  uint32_t height_shift = (format >> 24) & 0xF;
  uint32_t depth_shift = (format >> 28) & 0xF;
  uint32_t width = 1 << width_shift;
  uint32_t height = 1 << height_shift;
  uint32_t depth = 1 << depth_shift;

  VERBOSE_PRINT(
      ("Texture %d [0x%08X, %d x %d x %d (pitch register: 0x%X), format 0x%X]",
       stage, offset, width, height, depth, pitch, fmt_color));

  uint32_t adjusted_offset = offset;
  for (uint32_t layer = 0; layer < depth; ++layer) {
    StoreTextureLayer(info, store, stage, layer, adjusted_offset, width, height,
                      depth, pitch, format, control0, control1);
    adjusted_offset += pitch * height;
  }
}

void TraceTextures(const PushBufferCommandTraceInfo *info, StoreAuxData store) {
  for (uint32_t i = 0; i < 4; ++i) {
    StoreTextureStage(info, store, i);
  }
}

void TraceBegin(const PushBufferCommandTraceInfo *info, StoreAuxData store,
                const AuxConfig *config) {
  if (!config->texture_capture_enabled) {
    return;
  }

  uint32_t first_param;
  if (!GetParameter(info, 0, &first_param)) {
    DbgPrint("TraceBegin: Failed to retrieve parameter.\n");
    return;
  }

  if (first_param == NV097_SET_BEGIN_END_OP_END) {
    return;
  }

  DbgPrint("Begin %d\n", info->packet_index);
  TraceTextures(info, store);
}

void TraceEnd(const PushBufferCommandTraceInfo *info, StoreAuxData store,
              const AuxConfig *config) {
  if (!config->surface_depth_capture_enabled &&
      !config->surface_color_capture_enabled &&
      !config->raw_pgraph_capture_enabled && !config->raw_pfb_capture_enabled) {
    return;
  }
  uint32_t first_param;
  if (!GetParameter(info, 0, &first_param)) {
    DbgPrint("TraceEnd: Failed to retrieve parameter.\n");
    return;
  }

  if (first_param != NV097_SET_BEGIN_END_OP_END) {
    return;
  }

  DbgPrint("End %d\n", info->packet_index);
  TraceSurfaces(info, store, config);
}
