"""Check generated Pinyin ordering, lab terms, and font coverage."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / "asphalt-timer-4.3c" / "src"
data = (root / "pinyin_dict.cpp").read_text(encoding="utf-8")
common = (root / "pinyin_common.cpp").read_text(encoding="utf-8")
font = (root / "font_data.cpp").read_text(encoding="utf-8")
pairs = dict(re.findall(r'\{"([a-z]+)", "([^"]*)"\}', data))
assert list(pairs) == sorted(pairs)
for syllable, character in [
    ("li", "沥"), ("qing", "青"), ("shi", "试"), ("yan", "验"),
    ("zhen", "针"), ("ruan", "软"), ("nian", "黏"),
]:
    assert character in pairs[syllable], (syllable, character)
glyphs = {int(value, 16) for value in re.findall(r"\{0x([0-9A-F]+),", font)}
missing = {ord(char) for candidates in pairs.values() for char in candidates if ord(char) not in glyphs}
common_pairs = re.findall(r'\{"([a-z]+)", "([^"]*)"\}', common)
assert len(common_pairs) > 10000
assert len(common_pairs) == len(set(common_pairs))
assert all(ord(char) in glyphs for _, choices in common_pairs for char in choices if char != "|")
assert not missing, missing
print(f"OK: {len(pairs)} base syllables, {len(common_pairs)} common entries, {len(glyphs)} glyphs")
