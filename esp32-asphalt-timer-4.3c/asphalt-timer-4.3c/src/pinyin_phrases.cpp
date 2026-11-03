#include "pinyin_phrases.hpp"
#include "pinyin_common.hpp"

#include <string.h>

namespace {
struct Phrase { const char* py; const char* choices; };
constexpr Phrase kPhrases[] = {
  {"baowen", "保温"}, {"ceding", "测定"}, {"dianhuo", "点火"},
  {"dongliniandu", "动力黏度"}, {"hunheliao", "混合料"},
  {"jiare", "加热"}, {"jiaoban", "搅拌"}, {"jieguo", "结果"},
  {"jilu", "记录"}, {"lengque", "冷却"}, {"liqing", "沥青"},
  {"laohua", "老化"}, {"maxieer", "马歇尔"}, {"midu", "密度"},
  {"niandu", "黏度|粘度"}, {"ruanhuadian", "软化点"},
  {"shandian", "闪点"}, {"shijian", "时间"},
  {"shiyan", "试验|实验"}, {"shuiyu", "水浴"},
  {"wendu", "温度"}, {"xuanzhuan", "旋转"},
  {"yandu", "延度"}, {"yangpin", "样品"},
  {"zhenrudu", "针入度"}, {"zhiliang", "质量"},
};
}

const char* pinyin_phrase_choices(const char* input) {
  for (const Phrase& phrase : kPhrases)
    if (!strcmp(input, phrase.py)) return phrase.choices;
  return common_phrase_choices(input);
}

int pinyin_phrase_count(const char* choices) {
  if (!choices || !*choices) return 0;
  int count = 1;
  for (const char* p = choices; *p; ++p) if (*p == '|') ++count;
  return count;
}

bool pinyin_phrase_at(const char* choices, int index, char out[32]) {
  if (!choices || index < 0) return false;
  const char* start = choices;
  while (index > 0) {
    const char* split = strchr(start, '|');
    if (!split) return false;
    start = split + 1;
    --index;
  }
  const char* end = strchr(start, '|');
  const size_t length = end ? static_cast<size_t>(end - start) : strlen(start);
  if (!length || length >= 32) return false;
  memcpy(out, start, length);
  out[length] = 0;
  return true;
}
