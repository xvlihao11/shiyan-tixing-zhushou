#pragma once

#include "step_logic.hpp"

static constexpr int kMaxTemplates = 16;

struct TemplateStep {
  char name[32];
  uint16_t minutes;
  RemindMode mode;
};

struct LabTemplate {
  uint32_t id;
  char name[32];
  uint8_t count;
  TemplateStep steps[kMaxSteps];
};

struct TemplateData {
  uint32_t next_id;
  uint8_t count;
  LabTemplate items[kMaxTemplates];
};

extern TemplateData templates;

bool templates_load();
bool templates_save();
int templates_add();
bool templates_remove(int index);

