#pragma once

#include <stdint.h>

struct Glyph {
  uint32_t cp;
  uint8_t width;
  uint8_t height;
  uint8_t advance;
  uint32_t offset;
};

extern const int kGlyphCount;
extern const Glyph kGlyphs[];
extern const uint8_t kGlyphBits[];
