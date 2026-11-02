#pragma once
#include <stddef.h>
#include <stdint.h>
#include <Arduino.h>
bool records_write_file(const String& path,const String& content);
void record_time(int64_t ms, char* out, size_t size);
void records_begin(bool mounted);
bool records_sync();
bool records_ok();
uint32_t records_generation();
void records_force();
void records_print();
bool records_selftest();
