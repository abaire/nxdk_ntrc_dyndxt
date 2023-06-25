#include "texture_stage.h"

#include "debug_output.h"
#include "math3d.h"
#include "nxdk_ext.h"
#include "pbkit_ext.h"

// bitscan forward
static int bsf(int val){__asm bsf eax, val}

TextureStage::TextureStage() {
  matrix_unit(texture_matrix_);
}

bool TextureStage::RequiresColorspaceConversion() const {
  return format_.xbox_format ==
             NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 ||
         format_.xbox_format ==
             NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8;
}

void TextureStage::Commit(uint32_t memory_dma_offset,
                          uint32_t palette_dma_offset) const {
  if (!enabled_) {
    auto p = pb_begin();
    // NV097_SET_TEXTURE_CONTROL0
    p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(stage_), false);
    pb_end(p);
    return;
  }

  if (!format_.xbox_bpp) {
    PrintMsg(
        "No texture format specified. This will cause an invalid pgraph state "
        "exception and a crash.");
    ASSERT(!"No texture format specified. This will cause an invalid pgraph state exception and a crash.");
  }

  auto p = pb_begin();
  uint32_t offset =
      reinterpret_cast<uint32_t>(memory_dma_offset) + texture_memory_offset_;
  uint32_t texture_addr = offset & 0x03ffffff;
  // NV097_SET_TEXTURE_OFFSET
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_OFFSET(stage_), texture_addr);

  // NV097_SET_TEXTURE_CONTROL0
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(stage_),
               NV097_SET_TEXTURE_CONTROL0_ENABLE |
                   MASK(NV097_SET_TEXTURE_CONTROL0_ALPHA_KILL_ENABLE,
                        alpha_kill_enable_) |
                   MASK(NV097_SET_TEXTURE_CONTROL0_MIN_LOD_CLAMP, lod_min_) |
                   MASK(NV097_SET_TEXTURE_CONTROL0_MAX_LOD_CLAMP, lod_max_));

  uint32_t dimensionality = GetDimensionality();

  uint32_t size_u = bsf((int)size_u_);
  uint32_t size_v = bsf((int)size_v_);
  uint32_t size_p = 0;
  if (dimensionality > 2) {
    size_p = bsf((int)size_p_);
  }

  const uint32_t DMA_A = 1;
  //  const uint32_t DMA_B = 2;

  uint32_t format =
      MASK(NV097_SET_TEXTURE_FORMAT_CONTEXT_DMA, DMA_A) |
      MASK(NV097_SET_TEXTURE_FORMAT_CUBEMAP_ENABLE, cubemap_enable_) |
      MASK(NV097_SET_TEXTURE_FORMAT_BORDER_SOURCE, border_source_color_) |
      MASK(NV097_SET_TEXTURE_FORMAT_DIMENSIONALITY, dimensionality) |
      MASK(NV097_SET_TEXTURE_FORMAT_COLOR, format_.xbox_format) |
      MASK(NV097_SET_TEXTURE_FORMAT_MIPMAP_LEVELS, mipmap_levels_) |
      MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_U, size_u) |
      MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_V, size_v) |
      MASK(NV097_SET_TEXTURE_FORMAT_BASE_SIZE_P, size_p);

  // NV097_SET_TEXTURE_FORMAT
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_FORMAT(stage_), format);

  uint32_t pitch_param = (format_.xbox_bpp * width_ / 8) << 16;
  // NV097_SET_TEXTURE_CONTROL1
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_PITCH(stage_), pitch_param);

  uint32_t size_param = (width_ << 16) | (height_ & 0xFFFF);
  // NV097_SET_TEXTURE_IMAGE_RECT
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_NPOT_SIZE(stage_), size_param);

  // NV097_SET_TEXTURE_ADDRESS
  uint32_t texture_address =
      MASK(NV097_SET_TEXTURE_ADDRESS_U, wrap_modes_[0]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_U, cylinder_wrap_[0]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_V, wrap_modes_[1]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_V, cylinder_wrap_[1]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_P, wrap_modes_[2]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_P, cylinder_wrap_[2]) |
      MASK(NV097_SET_TEXTURE_ADDRESS_CYLINDERWRAP_Q, cylinder_wrap_[3]);
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_WRAP(stage_), texture_address);

  // NV097_SET_TEXTURE_FILTER
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_FILTER(stage_), texture_filter_);

  uint32_t palette_config = 0;
  if (format_.xbox_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
    ASSERT(palette_length_ <= 3 &&
           "Invalid attempt to use paletted format without setting palette.");
    uint32_t palette_offset = palette_dma_offset + palette_memory_offset_;
    palette_offset &= 0x03ffffc0;
    palette_config = MASK(NV097_SET_TEXTURE_PALETTE_CONTEXT_DMA, DMA_A) |
                     MASK(NV097_SET_TEXTURE_PALETTE_LENGTH, palette_length_) |
                     palette_offset;
  }

  // NV097_SET_TEXTURE_PALETTE
  p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_PALETTE_OFFSET(stage_),
               palette_config);

  p = pb_push1(p, NV097_SET_TEXTURE_BORDER_COLOR, border_color_);

  p = pb_push4f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_MAT, bump_env_material[0],
                bump_env_material[1], bump_env_material[2],
                bump_env_material[3]);
  p = pb_push1f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_SCALE, bump_env_scale);
  p = pb_push1f(p, NV097_SET_TEXTURE_SET_BUMP_ENV_OFFSET, bump_env_offset);
  p = pb_push1(p, NV097_SET_TEXTURE_MATRIX_ENABLE + (4 * stage_),
               texture_matrix_enable_);
  if (texture_matrix_enable_) {
    p = pb_push_4x4_matrix(p, NV097_SET_TEXTURE_MATRIX + 64 * stage_,
                           texture_matrix_);
  }

  p = pb_push1(p, NV097_SET_TEXGEN_S, texgen_s_);
  p = pb_push1(p, NV097_SET_TEXGEN_T, texgen_t_);
  p = pb_push1(p, NV097_SET_TEXGEN_R, texgen_r_);
  p = pb_push1(p, NV097_SET_TEXGEN_Q, texgen_q_);

  pb_end(p);
}

void TextureStage::SetFilter(uint32_t lod_bias,
                             TextureStage::ConvolutionKernel kernel,
                             TextureStage::MinFilter min,
                             TextureStage::MagFilter mag, bool signed_alpha,
                             bool signed_red, bool signed_green,
                             bool signed_blue) {
  texture_filter_ = MASK(NV097_SET_TEXTURE_FILTER_MIPMAP_LOD_BIAS, lod_bias) |
                    MASK(NV097_SET_TEXTURE_FILTER_CONVOLUTION_KERNEL, kernel) |
                    MASK(NV097_SET_TEXTURE_FILTER_MIN, min) |
                    MASK(NV097_SET_TEXTURE_FILTER_MAG, mag) |
                    MASK(NV097_SET_TEXTURE_FILTER_ASIGNED, signed_alpha) |
                    MASK(NV097_SET_TEXTURE_FILTER_RSIGNED, signed_red) |
                    MASK(NV097_SET_TEXTURE_FILTER_GSIGNED, signed_green) |
                    MASK(NV097_SET_TEXTURE_FILTER_BSIGNED, signed_blue);
}

int TextureStage::SetRawTexture(const uint8_t *source, uint32_t width,
                                uint32_t height, uint32_t depth, uint32_t pitch,
                                uint32_t bytes_per_pixel,
                                uint8_t *memory_base) const {
  uint8_t *dest = memory_base + texture_memory_offset_;
  memcpy(dest, source, pitch * height * depth);
  return 0;
}

int TextureStage::SetPalette(const uint32_t *palette, uint32_t length,
                             uint8_t *memory_base) {
  auto ret = SetPaletteSize(length);
  if (ret) {
    return ret;
  }

  uint8_t *dest = memory_base + palette_memory_offset_;
  memcpy(dest, palette, length * 4);
  return 0;
}

int TextureStage::SetPaletteSize(uint32_t length) {
  switch (length) {
    case 256:
      palette_length_ = 0;
      break;
    case 128:
      palette_length_ = 1;
      break;
    case 64:
      palette_length_ = 2;
      break;
    case 32:
      palette_length_ = 3;
      break;
    default:
      ASSERT(!"Invalid palette length. Must be 32|64|128|256");
      return 1;
  }
  return 0;
}

uint32_t TextureStage::GetDimensionality() const {
  if (height_ == 1 && depth_ == 1) {
    return 1;
  }
  if (depth_ > 1) {
    return 3;
  }
  return 2;
}
