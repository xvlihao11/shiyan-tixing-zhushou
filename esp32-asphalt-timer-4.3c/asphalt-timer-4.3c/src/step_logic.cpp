#include "step_logic.hpp"

#include <stdio.h>
#include <string.h>

namespace {

void copy_text(char* dest, size_t dest_len, const char* src) {
  if (dest_len == 0) return;
  if (!src) src = "";
  strncpy(dest, src, dest_len - 1);
  dest[dest_len - 1] = '\0';
}

}  // namespace

void step_clear(Step* step, const char* name, int minutes, RemindMode mode) {
  memset(step, 0, sizeof(*step));
  copy_text(step->name, sizeof(step->name), name);
  step->minutes = minutes;
  step->mode = mode;
}

bool step_finished(const Step& step) {
  return step.done > 0 || step.skipped > 0;
}

int current_index(const Run& run) {
  for (int i = 0; i < run.count; ++i) {
    if (run.steps[i].started > 0 && !step_finished(run.steps[i])) return i;
  }
  return -1;
}

int next_pending_index(const Run& run) {
  for (int i = 0; i < run.count; ++i) {
    if (!step_finished(run.steps[i]) && run.steps[i].started <= 0) return i;
  }
  return -1;
}

int64_t due_ms(const Step& step) {
  return step.started + static_cast<int64_t>(step.minutes) * 60000;
}

int64_t remaining_ms(const Step& step, int64_t now) {
  if (step.started <= 0 || step_finished(step)) return 0;
  return due_ms(step) - now;
}

bool reminder_active(const Step& step) {
  return step.mode != RemindMode::None && step.minutes > 0;
}

bool is_due(const Step& step, int64_t now) {
  if (step.started <= 0 || step_finished(step) || !reminder_active(step)) return false;
  return due_ms(step) <= now;
}

bool should_ring(const Step& step, int64_t now) {
  return step.mode == RemindMode::Alarm && is_due(step, now) && !step.notified;
}

bool should_notify(const Step& step, int64_t now) {
  return step.mode == RemindMode::Notification && is_due(step, now) && !step.notified;
}

StartResult start_step(Run& run, int index, int64_t now, bool confirm_skip) {
  if (index < 0 || index >= run.count) return StartResult::Missing;
  Step& step = run.steps[index];
  if (step.started > 0 || step_finished(step)) return StartResult::Already;
  bool earlier = false;
  for (int i = 0; i < index; ++i) {
    if (!step_finished(run.steps[i])) earlier = true;
  }
  if (earlier && !confirm_skip) return StartResult::NeedSkip;
  for (int i = 0; i < index; ++i) {
    if (step_finished(run.steps[i])) continue;
    // Preserve the real start time when an in-progress step is skipped.
    run.steps[i].skipped = now;
  }
  step.started = now;
  step.notified = false;
  return StartResult::Ok;
}

FinishResult finish_step(Run& run, int index, int64_t now, bool confirm_early) {
  if (index < 0 || index >= run.count) return FinishResult::NotStarted;
  Step& step = run.steps[index];
  if (step.started <= 0 || step_finished(step)) return FinishResult::NotStarted;
  const int64_t left = due_ms(step) - now;
  if (left > 0 && !confirm_early) return FinishResult::NeedEarly;
  step.done = now;
  return FinishResult::Ok;
}

void acknowledge(Step& step) { step.notified = true; }

WaitResult set_wait(Step& step, int minutes, RemindMode mode) {
  if (minutes < 0 || minutes > 1440) return WaitResult::Bad;
  if (mode != RemindMode::None && mode != RemindMode::Notification && mode != RemindMode::Alarm) {
    return WaitResult::Bad;
  }
  if (mode != RemindMode::None && minutes <= 0) return WaitResult::NeedMinutes;
  step.minutes = minutes;
  step.mode = mode;
  if (step.started > 0 && !step_finished(step)) step.notified = false;
  return WaitResult::Ok;
}

void status_text(const Run& run, char* out, size_t out_len) {
  const int current = current_index(run);
  if (current >= 0) {
    snprintf(out, out_len, "进行中 · %s", run.steps[current].name);
    return;
  }
  bool all_done = run.count > 0;
  for (int i = 0; i < run.count; ++i) {
    if (!step_finished(run.steps[i])) all_done = false;
  }
  if (all_done) {
    snprintf(out, out_len, "已结束");
    return;
  }
  const int pending = next_pending_index(run);
  snprintf(out, out_len, "待开始 · %s", pending >= 0 ? run.steps[pending].name : "");
}

void format_duration(int64_t ms, char* out, size_t out_len) {
  if (ms < 0) ms = -ms;
  const int64_t seconds = ms / 1000;
  const int hours = static_cast<int>(seconds / 3600);
  const int minutes = static_cast<int>((seconds % 3600) / 60);
  const int secs = static_cast<int>(seconds % 60);
  snprintf(out, out_len, "%d时%02d分%02d秒", hours, minutes, secs);
}

void make_penetration(Run& run, const char* sample) {
  memset(&run, 0, sizeof(run));
  strncpy(run.sample, sample, sizeof(run.sample) - 1);
  strncpy(run.name, "针入度", sizeof(run.name) - 1);
  run.count = 3;
  step_clear(&run.steps[0], "室温冷却", 90, RemindMode::Notification);
  step_clear(&run.steps[1], "水浴", 90, RemindMode::Notification);
  step_clear(&run.steps[2], "测定与录入读数", 0, RemindMode::None);
}

void make_heating(Run& run, const char* sample, int minutes) {
  memset(&run, 0, sizeof(run));
  strncpy(run.sample, sample, sizeof(run.sample) - 1);
  strncpy(run.name, "样品加热", sizeof(run.name) - 1);
  run.count = 1;
  step_clear(&run.steps[0], "加热", minutes, RemindMode::Alarm);
}

const char* mode_label(RemindMode mode) {
  switch (mode) {
    case RemindMode::Notification:
      return "消息";
    case RemindMode::Alarm:
      return "闹铃";
    default:
      return "不提醒";
  }
}

const char* phase_label(AgingPhase phase) {
  return phase==AgingPhase::Rtfot ? "RTFOT老化" : phase==AgingPhase::Tfot ? "TFOT老化" : "未老化";
}