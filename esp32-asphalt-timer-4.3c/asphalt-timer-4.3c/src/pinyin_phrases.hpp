#pragma once

const char* pinyin_phrase_choices(const char* input);
int pinyin_phrase_count(const char* choices);
bool pinyin_phrase_at(const char* choices, int index, char out[32]);
