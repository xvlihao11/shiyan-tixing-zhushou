#pragma once

#include <stdint.h>

int text_width(const char* utf8);
void draw_text(uint16_t* fb, int stride, int x, int y, const char* utf8, uint16_t color);
