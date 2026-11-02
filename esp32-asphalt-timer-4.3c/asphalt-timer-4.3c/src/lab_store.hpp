#pragma once

#include "step_logic.hpp"

static constexpr int kMaxSamples = 32;

struct Sample {
  uint32_t id;
  char name[32];
  int64_t created;
};

struct LabData {
  uint32_t next_id;
  uint16_t sample_count;
  uint16_t run_count;
  Sample samples[kMaxSamples];
  Run runs[kMaxRuns];
};

extern LabData lab;

bool lab_load();
bool lab_save();
uint32_t lab_generation();
int lab_add_sample(const char* name, int64_t now);
int lab_add_run(int sample_index, Run run);
bool lab_remove_sample(int sample_index);
bool lab_remove_run(int run_index);
int lab_find_sample(uint32_t id);
int lab_find_run(uint32_t id);
int lab_runs_for_sample(uint32_t sample_id);

