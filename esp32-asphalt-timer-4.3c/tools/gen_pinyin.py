"""Generate a compact simplified-Chinese pinyin dictionary from LVGL's MIT data."""

from __future__ import annotations

import ctypes
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"D:\ESP32-S3-Touch-LCD-4.3C\sketchbook\libraries\lvgl\src\extra\others\ime\lv_ime_pinyin.c")
OUTPUT = ROOT / "asphalt-timer-4.3c" / "src" / "pinyin_dict.cpp"

DOMAIN = {
    "li": "沥粒力理", "qing": "青轻清", "shi": "试实石时式湿", "yan": "验延研盐烟",
    "yang": "样养氧", "pin": "品拼", "jian": "检剪件建", "ce": "测侧", "wen": "温稳纹",
    "du": "度读毒", "re": "热", "lao": "老烙", "hua": "化花滑", "zhen": "针振真",
    "ru": "入乳如", "dian": "点电垫", "ruan": "软", "ya": "压亚", "cha": "差查插",
    "yu": "预余浴", "hun": "混", "he": "合核盒", "liao": "料聊", "xi": "稀细析",
    "hou": "厚后", "mi": "密米", "can": "参残", "shu": "数书输", "deng": "等灯",
    "dai": "待带", "zhong": "重中种", "liang": "量良凉", "jing": "净精静",
    "shui": "水", "gang": "钢", "rou": "柔", "nian": "黏年粘", "ji": "剂机基计",
    "su": "速塑", "duan": "断段", "lie": "裂", "hen": "痕", "fa": "发法", "hui": "灰回",
    "qiu": "球求", "huan": "环缓", "kuai": "快块", "jue": "绝决", "yuan": "圆原",
}


def simplify(value: str) -> str:
    if not value:
        return ""
    output = ctypes.create_unicode_buffer(len(value) * 2 + 1)
    count = ctypes.windll.kernel32.LCMapStringEx(
        "zh-CN", 0x02000000, value, len(value), output, len(output), None, None, 0
    )
    if count <= 0:
        raise RuntimeError(f"simplification failed for {value!r}")
    return output.value


def unique_hanzi(value: str) -> str:
    seen: set[str] = set()
    result = []
    for char in value:
        if "\u3400" <= char <= "\u9fff" and char not in seen:
            result.append(char)
            seen.add(char)
    return "".join(result)


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("lv_ime_pinyin_def_dict[]")
    end = text.index("};", start)
    entries = re.findall(r'\{\s*"([a-z]+)"\s*,\s*"([^"]*)"\s*\}', text[start:end])
    dictionary: dict[str, str] = {}
    for syllable, characters in entries:
        dictionary[syllable] = unique_hanzi(DOMAIN.get(syllable, "") + simplify(characters))
    for syllable, characters in DOMAIN.items():
        dictionary.setdefault(syllable, unique_hanzi(characters))

    lines = [
        '#include "pinyin_dict.hpp"',
        "// Derived from LVGL's MIT-licensed default Pinyin IME dictionary.",
        "// Simplified with the Windows LCMapStringEx API; domain terms sorted first.",
        "const PinyinEntry kPinyinDict[] = {",
    ]
    for syllable, characters in sorted(dictionary.items()):
        lines.append(f'  {{"{syllable}", "{characters}"}},')
    lines += ["};", f"const int kPinyinDictCount = {len(dictionary)};", ""]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUTPUT}: {len(dictionary)} syllables, {len(set(''.join(dictionary.values())))} characters")


if __name__ == "__main__":
    main()
