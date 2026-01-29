#include "pgraph_command_callbacks.h"

#include <stdio.h>
#include <string.h>

#include "fastmemcpy/fastmemcpy.h"
#include "pushbuffer_command.h"
#include "register_defs.h"
#include "xbdm.h"
#include "xbox_helper.h"
#include "xemu/hw/xbox/nv2a/nv2a_regs.h"

// #define VERBOSE_DEBUG

#ifdef VERBOSE_DEBUG
#define VERBOSE_PRINT(c) DbgPrint c
#else
#define VERBOSE_PRINT(c)
#endif

// #define ENABLE_PROFILING

#ifdef ENABLE_PROFILING
#include "util/profiler.h"

#define PROFILE_INIT() PROFILETOKEN __now
#define PROFILE_START() __now = ProfileStart()
#define PROFILE_SEND(msg)                                       \
  do {                                                          \
    double __elapsed = ProfileStop(&__now);                     \
    uint32_t __milliseconds = (uint32_t)__elapsed;              \
    uint32_t __fractional_milliseconds =                        \
        (uint32_t)((__elapsed - __milliseconds) * 1000.0);      \
    DbgPrint("PROFILE>> %s: %u.%u ms\n", (msg), __milliseconds, \
             __fractional_milliseconds);                        \
  } while (0)
#else
#define PROFILE_INIT()
#define PROFILE_START()
#define PROFILE_SEND(msg)
#endif

#define NV_PGRAPH_TILE_XBOX 0xFD400900
#define NV_PGRAPH_TLIMIT_XBOX 0xFD400904
#define NV_PGRAPH_TSIZE_XBOX 0xFD400908
#define NV_PGRAPH_ZCOMP_XBOX 0xFD400980
#define NV_PGRAPH_ZCOMP_OFFSET_XBOX 0xFD4009A0
#define NV_PGRAPH_CFG0_XBOX 0xFD4009A4
#define NV_PGRAPH_CFG1_XBOX 0xFD4009A8

static const uint32_t kTag = 0x6E744343;  // 'ntCC'

//! Value that may be added to contiguous memory addresses to access as
//! ADDR_AGPMEM, which is guaranteed to be linear (and thus may be slower than
//! tiled ADDR_FBMEM but can be manipulated directly).
static const uint32_t kAGPMemoryBase = 0xF0000000;
#define AGP_ADDR(a) (const uint8_t*)(kAGPMemoryBase | (a))

//! Value added to contiguous memory addresses to access as framebuffer memory.
// static const uint32_t kFramebufferMemoryBase = 0x80000000;
// #define FB_ADDR(a) (const uint8_t *)(kFramebufferMemoryBase | (a))

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
  uint32_t clip_x;
  uint32_t clip_y;
  uint32_t clip_w;
  uint32_t clip_h;
  uint32_t swizzle_param;
  BOOL swizzled;
} TextureParameters;

static void ApplyAntiAliasingFactor(uint32_t antialiasing_mode, uint32_t* x,
                                    uint32_t* y) {
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

static void ReadTextureParameters(TextureParameters* params) {
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

  //  uint32_t swizzle_unk2 = ReadDWORD(0xFD40086C);

  params->clip_x = surface_clip_x & 0xFFFF;
  params->clip_y = surface_clip_y & 0xFFFF;

  params->clip_w = (surface_clip_x >> 16) & 0xFFFF;
  params->clip_h = (surface_clip_y >> 16) & 0xFFFF;

  uint32_t swizzle_unk = ReadDWORD(0xFD400818);
  params->swizzle_param = swizzle_unk;

  BOOL surface_anti_aliasing = ((params->surface_type >> 4) & 3) != 0;

  ApplyAntiAliasingFactor(surface_anti_aliasing, &params->clip_x,
                          &params->clip_y);
  ApplyAntiAliasingFactor(surface_anti_aliasing, &params->clip_w,
                          &params->clip_h);

  params->width = params->clip_w;
  params->height = params->clip_h;

  params->swizzled = (params->surface_type & 3) == 2;

  // FIXME: if surface_type is 0, we probably can't even draw..
  uint32_t draw_format = ReadDWORD(0xFD400804);
  params->format_color = (draw_format >> 12) & 0xF;
  params->format_depth = (draw_format >> 18) & 0x3;

  // TODO: Support 3D surfaces.

  // TODO: Extract float state.
}

//! Stores the PGRAPH region.
static void StorePGRAPH(const PushBufferCommandTraceInfo* info,
                        StoreAuxData store) {
#define PGRAPH_REGION 0xFD400000
#define PGRAPH_REGION_SIZE 0x2000
  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(PGRAPH_REGION_SIZE, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading PGRAPH region.");
    return;
  }

  // 0xFD400200 hangs Xbox, but skipping 0x200 - 0x400 works.
  // TODO: Needs further testing which regions work.
  uint8_t* write_ptr = buffer;
  mmx_memcpy(write_ptr, (uint8_t*)PGRAPH_REGION, 0x200);
  write_ptr += 0x200;

  // Null out the unreadable bytes.
  memset(write_ptr, 0, 0x200);
  write_ptr += 0x200;

  mmx_memcpy(write_ptr, (uint8_t*)(PGRAPH_REGION + 0x400),
             PGRAPH_REGION_SIZE - 0x400);

  store(info, ADT_PGRAPH_DUMP, buffer, 0x2000);
  DmFreePool(buffer);
}

//! Stores the PFB region.
static void StorePFB(const PushBufferCommandTraceInfo* info,
                     StoreAuxData store) {
#define PFB_REGION 0xFD100000
#define PFB_REGION_SIZE 0x1000
  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(PFB_REGION_SIZE, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading PFB region.");
    return;
  }

  mmx_memcpy(buffer, (uint8_t*)PFB_REGION, PFB_REGION_SIZE);

  store(info, ADT_PFB_DUMP, buffer, 0x2000);
  DmFreePool(buffer);
}

#define NV10_PGRAPH_RDI_INDEX 0xFD400750
#define NV10_PGRAPH_RDI_DATA 0xFD400754

//! Stores RDI data.
static void StoreRDI(const PushBufferCommandTraceInfo* info, StoreAuxData store,
                     uint32_t offset, uint32_t count) {
  uint32_t buffer_size = sizeof(RDIHeader) + 4 * count;
  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint(
        "Error: Failed to allocate buffer when reading %u RDI values from "
        "offset 0x%X.",
        count, offset);
    return;
  }

  RDIHeader* header = (RDIHeader*)buffer;
  header->offset = offset;
  header->count = count;
  uint32_t* write_ptr = (uint32_t*)(buffer + sizeof(*header));

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

static void StoreSurface(const PushBufferCommandTraceInfo* info,
                         StoreAuxData store, SurfaceType type,
                         uint32_t surface_format, uint32_t surface_offset,
                         uint32_t width, uint32_t height, uint32_t pitch,
                         uint32_t clip_x, uint32_t clip_y, uint32_t clip_w,
                         uint32_t clip_h, BOOL swizzle, uint32_t swizzle_param,
                         const char* description) {
  uint32_t len = pitch * (clip_y + height);
  if (!len) {
    DbgPrint(
        "Error: calculated zero length when reading surface %d. W=%u H=%u P=%u "
        "clip=%u,%u,%u,%u",
        type, width, height, pitch, clip_x, clip_y, clip_w, clip_h);
    return;
  }
  uint32_t description_len = strlen(description);
  uint32_t buffer_size = sizeof(SurfaceHeader) + description_len + len;

  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading surface %d.", type);
    return;
  }

  SurfaceHeader* header = (SurfaceHeader*)buffer;
  header->type = type;
  header->format = surface_format;
  header->len = len;
  header->width = width;
  header->height = height;
  header->pitch = pitch;
  header->swizzle = swizzle;
  header->clip_x = clip_x;
  header->clip_y = clip_y;
  header->clip_width = clip_w;
  header->clip_height = clip_h;
  header->swizzle_param = swizzle_param;
  header->description_len = description_len;
  header->save_context.provoking_command = info->command.method;
  header->save_context.draw_index = info->draw_index;
  header->save_context.surface_dump_index = info->surface_dump_index;

  uint8_t* write_ptr = buffer + sizeof(*header);
  // null terminator is intentionally omitted.
  memcpy(write_ptr, description, description_len);  // NOLINT
  write_ptr += description_len;

  PROFILE_INIT();
  PROFILE_START();
  // TODO: Only read from AGP if needed, it is far slower than FB_ADDR reads.
  mmx_memcpy(write_ptr, AGP_ADDR(surface_offset), len);
  PROFILE_SEND("StoreSurface - AGP memcpy");

  PROFILE_START();
  store(info, ADT_SURFACE, buffer, buffer_size);
  PROFILE_SEND("StoreSurface - store");
  DmFreePool(buffer);
}

void TraceSurfaces(const PushBufferCommandTraceInfo* info, TraceContext* ctx,
                   StoreAuxData store, const AuxConfig* config) {
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

  PROFILE_INIT();
  PROFILE_START();
  TextureParameters params;
  ReadTextureParameters(&params);
  PROFILE_SEND("TraceSurfaces - ReadTextureParameters");

  if (!params.format_color) {
    DbgPrint("Warning: Invalid color format, skipping surface dump.");
    return;
  }

  if (config->surface_color_capture_enabled && params.color_offset) {
    char description[256];
    snprintf(description, sizeof(description),
             "%d x %d [pitch = %d (0x%X)], at 0x%08X, format 0x%X, type: 0x%X, "
             "swizzled: %s, clip: %d,%d,%d,%d",
             params.width, params.height, params.color_pitch,
             params.color_pitch, params.color_offset, params.format_color,
             params.surface_type, params.swizzled ? "Y" : "N", params.clip_x,
             params.clip_y, params.clip_w, params.clip_h);
    PROFILE_START();
    StoreSurface(info, store, ST_COLOR, params.format_color,
                 params.color_offset, params.width, params.height,
                 params.color_pitch, params.clip_x, params.clip_y,
                 params.clip_w, params.clip_h, params.swizzled,
                 params.swizzle_param, description);
    PROFILE_SEND("TraceSurfaces - Store color surface");
  }
  if (config->surface_depth_capture_enabled && params.depth_offset) {
    char description[256];
    snprintf(description, sizeof(description),
             "%d x %d [pitch = %d (0x%X)], at 0x%08X, format 0x%X, type: 0x%X, "
             "swizzled: %s, clip: %d,%d,%d,%d",
             params.width, params.height, params.depth_pitch,
             params.depth_pitch, params.depth_offset, params.format_depth,
             params.surface_type, params.swizzled ? "Y" : "N", params.clip_x,
             params.clip_y, params.clip_w, params.clip_h);
    PROFILE_START();
    StoreSurface(info, store, ST_DEPTH, params.format_depth,
                 params.depth_offset, params.width, params.height,
                 params.depth_pitch, params.clip_x, params.clip_y,
                 params.clip_w, params.clip_h, params.swizzled,
                 params.swizzle_param, description);
    PROFILE_SEND("TraceSurfaces - Store depth surface");
  }

  if (config->rdi_capture_enabled) {
    // Vertex shader instructions.
    PROFILE_START();
    StoreRDI(info, store, 0x100000, 136 * 4);
    PROFILE_SEND("TraceSurfaces - StoreRDI - shader");

    // Vertex shader constants 0 (192 four-element vectors).
    PROFILE_START();
    StoreRDI(info, store, 0x170000, 192 * 4);
    PROFILE_SEND("TraceSurfaces - StoreRDI - c0");

    // Vertex shader constants 1 (192 four-element vectors).
    PROFILE_START();
    StoreRDI(info, store, 0xCC0000, 192 * 4);
    PROFILE_SEND("TraceSurfaces - StoreRDI - c1");
  }

  ++ctx->surface_dump_index;
}

struct TextureFormatInfo {
  uint32_t format;
  uint32_t bytes_per_pixel;
  BOOL swizzled;
  BOOL linear;
};

static const struct TextureFormatInfo kTextureFormatInfo[] = {
    // swizzled
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8, 4, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8, 4, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8, 4, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8, 4, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8, 4, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5, 2, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5, 2, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5, 2, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4, 2, TRUE, FALSE},

    // linear unsigned
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8, 4, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8, 4, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8, 4, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8, 4, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8, 4, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8, 2, FALSE, TRUE},

    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED, 4, FALSE,
     TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16, 2, FALSE, TRUE},

    {NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8, 2, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8, 1, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8, 1, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8, 1, FALSE, TRUE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8, 1, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8, 1, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8, 1, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8, 4, TRUE, FALSE},

    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8, 2, TRUE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8, 2, TRUE, FALSE},

    // Compressed formats
    {NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5, 4, FALSE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8, 4, FALSE, FALSE},
    {NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8, 4, FALSE, FALSE},

    // Indexed formats
    {NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8, 1, TRUE, FALSE},

    // End tag.
    {0, 0, FALSE, FALSE},
};

#define PS_TEXTUREMODES_NONE 0x00L
#define PS_TEXTUREMODES_PROJECT2D 0x01L
#define PS_TEXTUREMODES_PROJECT3D 0x02L
#define PS_TEXTUREMODES_CUBEMAP 0x03L
#define PS_TEXTUREMODES_PASSTHRU 0x04L
#define PS_TEXTUREMODES_CLIPPLANE 0x05L
#define PS_TEXTUREMODES_BUMPENVMAP 0x06L
#define PS_TEXTUREMODES_BUMPENVMAP_LUM 0x07L
#define PS_TEXTUREMODES_BRDF 0x08L
#define PS_TEXTUREMODES_DOT_ST 0x09L
#define PS_TEXTUREMODES_DOT_ZW 0x0aL
#define PS_TEXTUREMODES_DOT_RFLCT_DIFF 0x0bL
#define PS_TEXTUREMODES_DOT_RFLCT_SPEC 0x0cL
#define PS_TEXTUREMODES_DOT_STR_3D 0x0dL
#define PS_TEXTUREMODES_DOT_STR_CUBE 0x0eL
#define PS_TEXTUREMODES_DPNDNT_AR 0x0fL
#define PS_TEXTUREMODES_DPNDNT_GB 0x10L
#define PS_TEXTUREMODES_DOTPRODUCT 0x11L
#define PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST 0x12L

static const struct TextureFormatInfo* GetFormatInfo(uint32_t texture_format) {
  const struct TextureFormatInfo* ret = kTextureFormatInfo;
  while (ret->bytes_per_pixel && ret->format != texture_format) {
    ++ret;
  }
  return ret;
}

static void StoreTextureLayer(const PushBufferCommandTraceInfo* info,
                              StoreAuxData store, uint32_t stage,
                              uint32_t layer, uint32_t adjusted_offset,
                              uint32_t width, uint32_t height, uint32_t depth,
                              uint32_t pitch, uint32_t format_register,
                              uint32_t format, uint32_t control0,
                              uint32_t control1, uint32_t image_rect,
                              uint32_t sampler_mode) {
  if (sampler_mode == PS_TEXTUREMODES_NONE ||
      sampler_mode == PS_TEXTUREMODES_PASSTHRU) {
    return;
  }

  const struct TextureFormatInfo* format_info = GetFormatInfo(format);
  if (!format_info->bytes_per_pixel) {
    DbgPrint("Error: failed to look up texture format 0x%X\n", format);
    return;
  }

  uint32_t len = 0;
  if (format_info->swizzled) {
    pitch = width * format_info->bytes_per_pixel;
    len = pitch * height;
  } else if (format_info->linear) {
    width = (image_rect >> 16) & 0x1FFF;
    height = image_rect & 0x1FFF;
    len = pitch * height;
  } else {
    // Reconstruct pitch from the compression type. DXT1 is 8 bytes per 4x4,
    // DXT3 and DXT5 are 16.
    uint32_t block_width = (width + 3) / 4;
    uint32_t block_height = (height + 3) / 4;
    if (format == NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5) {
      pitch = block_width << 3;
    } else {
      pitch = block_width << 4;
    }

    len = pitch * block_height;
  }

  if (!len) {
    DbgPrint(
        "Error: calculated zero length when reading texture %u:%u. W=%u H=%u "
        "P=%u",
        stage, layer, width, height, pitch);
    return;
  }
  uint32_t buffer_size = sizeof(TextureHeader) + len;

  uint8_t* buffer = (uint8_t*)DmAllocatePoolWithTag(buffer_size, kTag);
  if (!buffer) {
    DbgPrint("Error: Failed to allocate buffer when reading texture %u:%u.",
             stage, layer);
    return;
  }

  TextureHeader* header = (TextureHeader*)buffer;
  header->stage = stage;
  header->layer = layer;

  header->save_context.provoking_command = info->command.method;
  header->save_context.draw_index = info->draw_index;
  header->save_context.surface_dump_index = info->surface_dump_index;

  header->len = len;
  header->format = format_register;
  header->width = width;
  header->height = height;
  header->depth = depth;
  header->pitch = pitch;
  header->control0 = control0;
  header->control1 = control1;
  header->image_rect = image_rect;

  uint8_t* write_ptr = buffer + sizeof(*header);
  mmx_memcpy(write_ptr, AGP_ADDR(adjusted_offset), len);

  store(info, ADT_TEXTURE, buffer, buffer_size);
  DmFreePool(buffer);
}

#define TEXTURE_CTRL_ENABLE (1 << 30)
static void StoreTextureStage(const PushBufferCommandTraceInfo* info,
                              StoreAuxData store, uint32_t stage) {
  // Verify that the stage is enabled.
  uint32_t reg_offset = stage * 4;
  uint32_t control0 = ReadDWORD(PGRAPH_TEXCTL0_0 + reg_offset);
  if (!(control0 & TEXTURE_CTRL_ENABLE)) {
    return;
  }

  // Check the sampler format to ensure that the texture data has meaning.
  uint32_t sampler_mode = (ReadDWORD(PGRAPH_SHADERPROG) >> (stage * 5)) & 0x1F;

  uint32_t offset = ReadDWORD(PGRAPH_TEXOFFSET0 + reg_offset);
  uint32_t control1 = ReadDWORD(PGRAPH_TEXCTL1_0 + reg_offset);
  uint32_t pitch = (control1 >> 16) & 0xFFFF;
  uint32_t format = ReadDWORD(PGRAPH_TEXFMT0 + reg_offset);
  uint32_t image_rect = ReadDWORD(PGRAPH_TEXIMAGERECT0 + reg_offset);
  uint32_t texture_type = (format >> 8) & 0x7F;

  uint32_t width_shift = (format >> 20) & 0xF;
  uint32_t height_shift = (format >> 24) & 0xF;
  uint32_t depth_shift = (format >> 28) & 0xF;
  uint32_t width = 1 << width_shift;
  uint32_t height = 1 << height_shift;
  uint32_t depth = 1 << depth_shift;

  VERBOSE_PRINT(
      ("Texture %d [0x%08X, %d x %d x %d (pitch register: 0x%X), format 0x%X] "
       "Sampler 0x%X",
       stage, offset, width, height, depth, pitch, (format >> 8) & 0x7F,
       sampler_mode));

  uint32_t adjusted_offset = offset;
  for (uint32_t layer = 0; layer < depth; ++layer) {
    StoreTextureLayer(info, store, stage, layer, adjusted_offset, width, height,
                      depth, pitch, format, texture_type, control0, control1,
                      image_rect, sampler_mode);
    adjusted_offset += pitch * height;
  }
}

void TraceTextures(const PushBufferCommandTraceInfo* info, StoreAuxData store) {
  for (uint32_t i = 0; i < 4; ++i) {
    StoreTextureStage(info, store, i);
  }
}

void TraceBegin(const PushBufferCommandTraceInfo* info, TraceContext* ctx,
                StoreAuxData store, const AuxConfig* config) {
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

  DbgPrint("BEGIN - Packet: %d Draw: %u Surface: %u\n", info->packet_index,
           info->draw_index, info->surface_dump_index);
  TraceTextures(info, store);
}

void TraceEnd(const PushBufferCommandTraceInfo* info, TraceContext* ctx,
              StoreAuxData store, const AuxConfig* config) {
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

  DbgPrint("END - Packet: %d Draw: %u Surface: %u\n", info->packet_index,
           info->draw_index, info->surface_dump_index);
  ++ctx->draw_index;
  TraceSurfaces(info, ctx, store, config);
}
