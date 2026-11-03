#include "pinyin_dict.hpp"
#include "pinyin_common.hpp"

#include <string.h>

const char* pinyin_candidates(const char* syllable) {
  if (const char* common = common_pinyin_candidates(syllable)) return common;
  int lo = 0, hi = kPinyinDictCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int order = strcmp(syllable, kPinyinDict[mid].syllable);
    if (!order) return kPinyinDict[mid].characters;
    if (order < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return nullptr;
}

int pinyin_candidate_count(const char* candidates) {
  int count = 0;
  if (!candidates) return 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(candidates); *p; ++p)
    if ((*p & 0xC0) != 0x80) ++count;
  return count;
}

bool pinyin_candidate_at(const char* candidates, int index, char out[5]) {
  if (!candidates || index < 0) return false;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(candidates);
  while (*p && index > 0) {
    ++p;
    while ((*p & 0xC0) == 0x80) ++p;
    --index;
  }
  if (!*p) return false;
  int length = 1;
  if ((*p & 0xE0) == 0xC0) length = 2;
  else if ((*p & 0xF0) == 0xE0) length = 3;
  else if ((*p & 0xF8) == 0xF0) length = 4;
  memcpy(out, p, length);
  out[length] = 0;
  return true;
}
