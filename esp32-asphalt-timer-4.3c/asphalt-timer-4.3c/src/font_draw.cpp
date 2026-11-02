#include "font_draw.hpp"

#include "font_data.hpp"

namespace {

uint32_t decode_utf8(const char*& text) {
  const uint8_t c = static_cast<uint8_t>(*text);
  if (c == 0) return 0;
  if (c < 0x80) {
    ++text;
    return c;
  }
  if ((c & 0xE0) == 0xC0 && text[1]) {
    const uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(text[1]) & 0x3F);
    text += 2;
    return cp;
  }
  if ((c & 0xF0) == 0xE0 && text[1] && text[2]) {
    const uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<uint8_t>(text[1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(text[2]) & 0x3F);
    text += 3;
    return cp;
  }
  ++text;
  return 0xFFFD;
}

const Glyph* find_glyph(uint32_t cp) {
  int lo = 0;
  int hi = kGlyphCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (kGlyphs[mid].cp == cp) return &kGlyphs[mid];
    if (kGlyphs[mid].cp < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

}  // namespace

int text_width(const char* utf8) {
  int width = 0;
  const char* cursor = utf8 ? utf8 : "";
  while (*cursor) {
    const Glyph* glyph = find_glyph(decode_utf8(cursor));
    width += glyph ? glyph->advance : 12;
  }
  return width;
}

void draw_text(uint16_t* fb, int stride, int x, int y, const char* utf8, uint16_t color) {
  if (!fb || !utf8) return;
  int pen = x;
  const char* cursor = utf8;
  while (*cursor) {
    const uint32_t cp = decode_utf8(cursor);
    const Glyph* glyph = find_glyph(cp);
    if (!glyph) {
      pen += 12;
      continue;
    }
    const uint8_t* bits = kGlyphBits + glyph->offset;
    for (int row = 0; row < glyph->height; ++row) {
      const int py = y + row;
      if (py < 0 || py >= 480) continue;
      for (int col = 0; col < glyph->width; ++col) {
        const int px = pen + col;
        if (px < 0 || px >= 800) continue;
        const int bit = row * glyph->width + col;
        if (bits[bit >> 3] & (0x80 >> (bit & 7))) fb[py * stride + px] = color;
      }
    }
    pen += glyph->advance;
  }
}
