#pragma once

#include <stddef.h>
#include <stdint.h>

// Same rules as the phone app in app/assets/index.html:
// current(), stepFinished(), startStep(), finishStep(), planAlarm() due/ring.
// Time is unix milliseconds. 0 means "not set" for started/done/skipped.

enum class AgingPhase : uint8_t { Original=0, Rtfot=1, Tfot=2 };
const char* phase_label(AgingPhase phase);

enum class RemindMode : uint8_t { None = 0, Notification = 1, Alarm = 2 };

enum class StartResult : uint8_t { Ok, Missing, Already, NeedSkip };
enum class FinishResult : uint8_t { Ok, NotStarted, NeedEarly };
enum class WaitResult : uint8_t { Ok, Bad, NeedMinutes };

static constexpr int kMaxSteps = 6;
static constexpr int kMaxRuns = 32;

struct Step {
  char name[32];
  int minutes;
  RemindMode mode;
  int64_t started;
  int64_t done;
  int64_t skipped;
  bool notified;
};

struct Run {
  uint32_t id;
  uint32_t sample_id;
  char sample[24];
  char name[24];
  Step steps[kMaxSteps];
  int count;
  AgingPhase phase;
};

void step_clear(Step* step, const char* name, int minutes, RemindMode mode);
bool step_finished(const Step& step);
int current_index(const Run& run);
int next_pending_index(const Run& run);
int64_t due_ms(const Step& step);
int64_t remaining_ms(const Step& step, int64_t now);
bool reminder_active(const Step& step);
bool is_due(const Step& step, int64_t now);
bool should_ring(const Step& step, int64_t now);
bool should_notify(const Step& step, int64_t now);
StartResult start_step(Run& run, int index, int64_t now, bool confirm_skip);
FinishResult finish_step(Run& run, int index, int64_t now, bool confirm_early);
void acknowledge(Step& step);
WaitResult set_wait(Step& step, int minutes, RemindMode mode);
void status_text(const Run& run, char* out, size_t out_len);
void format_duration(int64_t ms, char* out, size_t out_len);
void make_penetration(Run& run, const char* sample);
void make_heating(Run& run, const char* sample, int minutes);
const char* mode_label(RemindMode mode);
