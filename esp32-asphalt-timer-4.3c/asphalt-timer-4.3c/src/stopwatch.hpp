#pragma once
#include <stdint.h>

// Monotonic microseconds, independent of RTC changes and the millis() rollover.
struct Stopwatch {
  static constexpr int kCacheRecords = 100;
  uint64_t accumulated_us = 0;
  uint64_t started_us = 0;
  uint64_t records_us[kCacheRecords] = {};
  uint32_t record_count = 0;
  bool running = false;

  uint64_t elapsed_us(uint64_t now) const {
    return accumulated_us + (running ? now - started_us : 0);
  }
  void start(uint64_t now) {
    if (running) return;
    started_us = now;
    running = true;
  }
  void pause(uint64_t now) {
    if (!running) return;
    accumulated_us = elapsed_us(now);
    running = false;
  }
  bool reset() {
    if (running) return false;
    accumulated_us = started_us = 0;
    record_count = 0;
    return true;
  }
  bool record(uint64_t now) {
    if (!running || record_count == UINT32_MAX) return false;
    records_us[record_count % kCacheRecords] = elapsed_us(now);
    ++record_count;
    return true;
  }
  bool cached(uint32_t index,uint64_t& value) const {
    if(index>=record_count || record_count-index>kCacheRecords)return false;
    value=records_us[index % kCacheRecords];return true;
  }
  uint64_t interval_us(uint32_t index) const {
    uint64_t total=0,previous=0;
    return cached(index,total) && (!index || cached(index-1,previous)) ? total-previous : 0;
  }
};

inline bool stopwatch_selftest() {
  Stopwatch s;
  if (s.elapsed_us(900) != 0 || s.running) return false;
  s.start(1000); s.start(2000);
  if (s.elapsed_us(1235567) != 1234567 || s.reset()) return false;
  if (!s.record(501000) || !s.record(1251000) || s.interval_us(0) != 500000 ||
      s.interval_us(1) != 750000 || s.records_us[1] != 1250000) return false;
  s.pause(2001000); s.pause(9000000);
  if (s.elapsed_us(99000000) != 2000000 || s.running || s.record(99000000)) return false;
  s.start(100000000);
  if (s.elapsed_us(100500000) != 2500000) return false;
  if (!s.record(100500000) || s.interval_us(2) != 1250000) return false;
  s.pause(101000000);
  if (s.accumulated_us != 3000000 || !s.reset() || s.elapsed_us(102000000) || s.record_count) return false;
  const uint64_t long_run = uint64_t(60) * 86400 * 1000000;
  s.start(42); s.pause(42 + long_run);
  if (s.elapsed_us(42 + long_run + 1) != long_run) return false;
  s.start(42 + long_run + 2);
  for (int i=0;i<Stopwatch::kCacheRecords;++i) if (!s.record(42 + long_run + 3 + i)) return false;
  if(!s.record(42 + long_run + 200) || s.record_count != Stopwatch::kCacheRecords+1)return false;
  uint64_t value=0;
  return !s.cached(0,value) && s.cached(100,value) && value==long_run+198;
}
