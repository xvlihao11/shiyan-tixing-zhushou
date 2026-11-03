#pragma once
#include <Arduino.h>
#include "stopwatch.hpp"

// A bounded RAM cache with an append-only, verified CSV backing file.
class StopwatchStore {
 public:
  bool append(const Stopwatch& watch,uint64_t elapsed);
  bool read(const Stopwatch& watch,uint32_t index,uint64_t& total,uint64_t& interval);
  void reset();
  const String& path() const { return path_; }
  const char* error() const { return error_; }
  bool saved(uint32_t count) const { return count>0 && persisted_==count; }
 private:
  bool create();
  bool write_record(uint32_t index,uint64_t total,uint64_t interval);
  bool read_record(uint32_t index,uint64_t& total,uint64_t& interval);
  String path_;
  uint32_t persisted_=0;
  bool fault_=false;
  const char* error_="";
};
bool stopwatch_store_selftest();
