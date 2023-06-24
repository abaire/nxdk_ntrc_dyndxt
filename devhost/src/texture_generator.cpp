#include "texture_generator.h"

void GenerateRGBACheckerboard(void *target, uint32_t x_offset,
                              uint32_t y_offset, uint32_t width,
                              uint32_t height, uint32_t pitch,
                              uint32_t first_color, uint32_t second_color,
                              uint32_t checker_size) {
  auto buffer = reinterpret_cast<uint8_t *>(target);
  auto odd = first_color;
  auto even = second_color;
  buffer += y_offset * pitch;

  for (uint32_t y = 0; y < height; ++y) {
    auto pixel = reinterpret_cast<uint32_t *>(buffer);
    pixel += x_offset;
    buffer += pitch;

    if (!(y % checker_size)) {
      auto temp = odd;
      odd = even;
      even = temp;
    }

    for (uint32_t x = 0; x < width; ++x) {
      *pixel++ = ((x / checker_size) & 0x01) ? odd : even;
    }
  }
}

void GenerateRGBATestPattern(void *target, uint32_t width, uint32_t height) {
  auto pixels = static_cast<uint32_t *>(target);
  for (uint32_t y = 0; y < height; ++y) {
    auto y_normal = static_cast<uint32_t>(static_cast<float>(y) * 255.0f /
                                          static_cast<float>(height));

    for (uint32_t x = 0; x < width; ++x, ++pixels) {
      auto x_normal = static_cast<uint32_t>(static_cast<float>(x) * 255.0f /
                                            static_cast<float>(width));
      *pixels = y_normal + (x_normal << 8) + ((255 - y_normal) << 16) +
                ((x_normal + y_normal) << 24);
    }
  }
}

void GenerateColoredCheckerboard(void *target, uint32_t x_offset,
                                 uint32_t y_offset, uint32_t width,
                                 uint32_t height, uint32_t pitch,
                                 const uint32_t *colors, uint32_t num_colors,
                                 uint32_t checker_size) {
  auto buffer = reinterpret_cast<uint8_t *>(target);
  buffer += y_offset * pitch;

  uint32_t row_color_index = 0;

  for (uint32_t y = 0; y < height; ++y) {
    auto pixel = reinterpret_cast<uint32_t *>(buffer);
    pixel += x_offset;
    buffer += pitch;

    if (y && !(y % checker_size)) {
      row_color_index = ++row_color_index % num_colors;
    }

    auto color_index = row_color_index;
    auto color = colors[color_index];
    for (uint32_t x = 0; x < width; ++x) {
      if (x && !(x % checker_size)) {
        color_index = ++color_index % num_colors;
        color = colors[color_index];
      }
      *pixel++ = color;
    }
  }
}
