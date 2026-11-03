#pragma once

struct PinyinEntry {
  const char* syllable;
  const char* characters;
};

extern const PinyinEntry kPinyinDict[];
extern const int kPinyinDictCount;

const char* pinyin_candidates(const char* syllable);
int pinyin_candidate_count(const char* candidates);
bool pinyin_candidate_at(const char* candidates, int index, char out[5]);
