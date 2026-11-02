"""Build an offline Pinyin lexicon from MIT-licensed jieba and pypinyin data."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "asphalt-timer-4.3c" / "src" / "pinyin_common.cpp"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", type=Path, required=True, help="directory containing jieba and pypinyin")
    args = parser.parse_args()
    sys.path.insert(0, str(args.packages))
    from pypinyin import Style, lazy_pinyin  # type: ignore
    from gen_pinyin import DOMAIN

    words: list[tuple[int, str]] = []
    singles: list[tuple[int, str]] = []
    dictionary = args.packages / "jieba" / "dict.txt"
    for line in dictionary.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) < 2 or not parts[1].isdigit():
            continue
        word, frequency = parts[0], int(parts[1])
        if not all("\u4e00" <= char <= "\u9fff" for char in word):
            continue
        if len(word) == 1:
            singles.append((frequency, word))
        elif 2 <= len(word) <= 4:
            words.append((frequency, word))
    singles.sort(reverse=True)
    words.sort(reverse=True)

    def reading(word: str) -> str | None:
        values = lazy_pinyin(word, style=Style.NORMAL, errors="ignore")
        result = "".join(value.replace("ü", "v").replace("u:", "v") for value in values)
        return result if re.fullmatch(r"[a-z]+", result) and len(result) <= 31 else None

    char_groups: dict[str, list[str]] = defaultdict(list)
    for _, char in singles[:4200]:
        py = reading(char)
        if py and char not in char_groups[py]:
            char_groups[py].append(char)
    for py, chars in DOMAIN.items():
        group = char_groups[py]
        char_groups[py] = list(dict.fromkeys(chars + "".join(group)))

    phrase_groups: dict[str, list[str]] = defaultdict(list)
    for _, word in words[:16000]:
        py = reading(word)
        if py and word not in phrase_groups[py] and len(phrase_groups[py]) < 5:
            phrase_groups[py].append(word)

    lines = [
        '#include "pinyin_common.hpp"',
        "#include <string.h>",
        "// Generated from jieba 0.42.1 word frequencies and pypinyin 0.55.0 readings (MIT).",
        "namespace {",
        "struct Entry { const char* py; const char* choices; };",
        "const Entry kChars[] = {",
    ]
    for py, chars in sorted(char_groups.items()):
        lines.append(f'  {{"{py}", "{"".join(chars)}"}},')
    lines += ["};", "const Entry kPhrases[] = {"]
    for py, phrases in sorted(phrase_groups.items()):
        lines.append(f'  {{"{py}", "{"|".join(phrases)}"}},')
    lines += [
        "};",
        "template <size_t N> const char* find(const Entry (&entries)[N], const char* py) {",
        "  int lo = 0, hi = static_cast<int>(N) - 1;",
        "  while (lo <= hi) {",
        "    const int mid = (lo + hi) / 2;",
        "    const int order = strcmp(py, entries[mid].py);",
        "    if (!order) return entries[mid].choices;",
        "    if (order < 0) hi = mid - 1; else lo = mid + 1;",
        "  }",
        "  return nullptr;",
        "}",
        "}",
        "const char* common_pinyin_candidates(const char* input) { return find(kChars, input); }",
        "const char* common_phrase_choices(const char* input) { return find(kPhrases, input); }",
        "",
    ]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    glyphs = set("".join("".join(group) for group in char_groups.values()))
    glyphs.update("".join(word for group in phrase_groups.values() for word in group))
    print(f"wrote {OUTPUT}: {len(char_groups)} syllables, {len(phrase_groups)} phrase keys, {len(glyphs)} characters")


if __name__ == "__main__":
    main()
