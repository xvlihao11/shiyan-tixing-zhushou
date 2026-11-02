"""Rasterize the glyphs used by the firmware into font_data.cpp."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "asphalt-timer-4.3c" / "src"
FONT_PATH = Path(r"C:\Windows\Fonts\simhei.ttf")
SIZE = 22
CELL = 24


def unescape(text: str) -> str:
    out = []
    index = 0
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"'}.get(text[index + 1], text[index + 1]))
            index += 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def codepoints() -> list[int]:
    chars = set(range(32, 127))
    blob = "\n".join(path.read_text(encoding="utf-8") for path in SRC.glob("*.cpp"))
    blob += "\n".join(path.read_text(encoding="utf-8") for path in SRC.glob("*.hpp"))
    blob += "\n".join(path.read_text(encoding="utf-8") for path in ROOT.glob("asphalt-timer-4.3c/*.ino"))
    for match in re.finditer(r'"((?:\\.|[^"\\])*)"', blob):
        text = unescape(match.group(1))
        chars.update(ord(ch) for ch in text)
    chars.discard(0)
    return sorted(cp for cp in chars if cp >= 32)


def raster(font: ImageFont.FreeTypeFont, cp: int) -> tuple[int, int, bytes]:
    image = Image.new("1", (CELL, CELL), 0)
    draw = ImageDraw.Draw(image)
    glyph = chr(cp)
    bbox = draw.textbbox((0, 0), glyph, font=font)
    width = max(1, min(CELL, bbox[2] - bbox[0]))
    draw.text((1, 1 - bbox[1]), glyph, font=font, fill=1)
    bits = bytearray()
    accumulator = 0
    count = 0
    for y in range(CELL):
        for x in range(width):
            accumulator = (accumulator << 1) | (1 if image.getpixel((x, y)) else 0)
            count += 1
            if count == 8:
                bits.append(accumulator)
                accumulator = 0
                count = 0
    if count:
        bits.append(accumulator << (8 - count))
    advance = min(CELL, max(width + 1, 8 if cp < 128 else width + 2))
    return advance, width, bytes(bits)


def main() -> None:
    font = ImageFont.truetype(str(FONT_PATH), SIZE)
    glyphs = []
    blob = bytearray()
    for cp in codepoints():
        advance, width, bits = raster(font, cp)
        glyphs.append((cp, width, CELL, advance, len(blob)))
        blob.extend(bits)
    out = SRC / "font_data.cpp"
    lines = [
        '#include "font_data.hpp"',
        "",
        f"const int kGlyphCount = {len(glyphs)};",
        "const Glyph kGlyphs[] = {",
    ]
    for cp, width, height, advance, offset in glyphs:
        lines.append(f"  {{0x{cp:04X}, {width}, {height}, {advance}, {offset}}},")
    lines.append("};")
    hex_bytes = ", ".join(f"0x{b:02X}" for b in blob)
    lines.append(f"const uint8_t kGlyphBits[] = {{{hex_bytes}}};")
    lines.append("")
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out} glyphs={len(glyphs)} bytes={len(blob)}")


if __name__ == "__main__":
    sys.exit(main())
