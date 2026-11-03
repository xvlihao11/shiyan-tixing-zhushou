#include <Arduino.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <esp_timer.h>
#include <nvs.h>
#include <SD_MMC.h>
#include "src/rgb_lcd_port/rgb_lcd_port.h"
#include "src/gt911/gt911.h"
#include "src/rtc_pcf85063a/rtc_pcf85063a.h"
#include "src/speaker_microphone/codec_dev.h"
#include "src/speaker_microphone/speaker_microphone.h"
#include "src/font_draw.hpp"
#include "src/lab_store.hpp"
#include "src/lab_codec.hpp"
#include "src/run_age.hpp"
#include "src/history.hpp"
#include "src/pocket_guard.hpp"
#include "src/stopwatch.hpp"
#include "src/stopwatch_store.hpp"
#include "src/record_press.hpp"
#include "src/tf_records.hpp"
#include "src/result_store.hpp"
#include "src/transfer.hpp"
#include "src/template_store.hpp"
#include "src/pinyin_dict.hpp"
#include "src/pinyin_phrases.hpp"

// RGB565 palette. The app redraws only after interaction or once per second while visible.
constexpr uint16_t BG = 0x10A3, CARD = 0x2126, RAISED = 0x2968;
constexpr uint16_t KEY_BG = 0x0000;
constexpr uint16_t WHITE_ = 0xFFFF, MUTED = 0xB596, CYAN = 0x7F3F;
constexpr uint16_t BLUE = 0x24DA, GREEN = 0x34D3, RED = 0xD9C8, ORANGE = 0xFBA0;
constexpr int W = 800, H = 480;
uint16_t* pixels = nullptr;
uint16_t* frame_a = nullptr;
uint16_t* frame_b = nullptr;

enum class Page : uint8_t {
  Dashboard, Samples, Sample, Run, NewSample, PickProject, NewProject,
  Templates, TemplateEdit, EditText, Clock, Settings, SampleDates, SampleRecords, StepRecord, Results, ResultOptions, NumericPad, ResultSummary, ExportSelect, Transfer, ResultStandards, PageJump, ExportDates, ExportSamples, HistoryList, HistoryDetail, HistoryDate, HistoryField, Stopwatch, StopwatchRecords
};
enum class Action : uint8_t {
  None, Home, Samples, Back, Next, Prev, OpenSample, OpenRun, NewSample,
  SaveSample, PickProject, AddPreset, NewProject, SaveProject, Key, Backspace,
  StepAction, StepMinus, StepPlus, StepMode, DeleteSample, DeleteRun,
  Confirm, Cancel, TaskTab, StopAlarm, FinishAlarm, ClockField, ClockMinus,
  ClockPlus, ClockSave, Brightness, OpenTemplates, AddTemplate, EditTemplate,
  UseTemplate, TemplateName, TemplateStepName, TemplateAddStep,
  TemplateDeleteStep, DeleteTemplate, SaveText, CommonPhrase, SelectStep,
  TemplateMinutes, RunMinutes, ProjectMinutes, PinyinToggle,
  PinyinCandidate, PinyinNext, PinyinPrev, PinyinClear,
  OpenSettings, BrightnessMinus, BrightnessPlus, VolumeMinus, VolumePlus, TestSound, ProjectPhase, RunPhase, OpenDate, PocketLock, SampleRecords, OpenRecord,
  ResultOpen, ResultOptions, ResultGroupName, ResultStandard, ResultNotes, ResultGroup,
  ResultAddGroup, ResultRemoveGroup, ResultAddReading, ResultRemoveReading, ResultNumber,
  NumberKey, NumberConfirm, ResultMetric, ResultUnit, ResultMode, ResultRule, ResultBaseline,
  ResultSummary, SummaryPage, SummaryOpen, ExportOpen, ExportToggle, ExportScope, ExportTimes, ExportGenerate,
  TransferStart, TransferStop, TransferQR, MobileShare, StandardChoose, StandardCustom, JumpOpen, JumpKey, JumpGo,
  ExportDateOpen, ExportSampleOpen, ExportDateToggle, ExportSampleToggle, ExportRoot,
  HistoryOpen, HistoryItemOpen, HistoryRestore, HistoryExport, HistoryDateOpen, HistoryDateKey, HistoryDateApply, HistoryAll, HistoryFieldOpen, HistoryExportRow,
  StopwatchOpen, StopwatchToggle, StopwatchReset, StopwatchRecord, StopwatchRecords, StopwatchPrimary
};
// Archive, NVS and TF operations may nest; leave headroom for their call chains.
SET_LOOP_TASK_STACK_SIZE(20480);
struct Hit { int x, y, w, h; Action action; int arg; };
Hit hits[90];
int hit_count = 0;
Page page = Page::Dashboard;
Stopwatch stopwatch;
StopwatchStore stopwatch_store;
RecordPress record_press;
uint32_t record_flash_until=0;
bool record_flash_failed=false;
bool record_flash_started=false,record_flash_resumed=false;
bool record_audio_open=false;
int record_tone_remaining=0,record_tone_phase=0;
uint32_t record_audio_mute_at=0;
uint32_t record_audio_test_until=0;
Page stopwatch_return = Page::Settings;
int selected_sample = -1, selected_run = -1, selected_template = -1;
int selected_step = 0, list_page = 0, step_page = 0;
Page jump_parent=Page::Dashboard;
int jump_total=1;
char jump_entry[10]={};
int task_tab = 0;  // 0: active timers, 1: due timers.
int pending_run = -1, pending_step = -1;
enum class ConfirmKind : uint8_t { None, Skip, Early, DeleteSample, DeleteRun, DeleteTemplate, DeleteStep, RestoreHistory, ResetStopwatch };
ConfirmKind confirm_kind = ConfirmKind::None;
char entry[32] = {};
int project_minutes = 30;
AgingPhase project_phase = AgingPhase::Original;
int selected_date = -1;
enum class EditTarget : uint8_t { None, TemplateName, StepName, Minutes, RunMinutes, ProjectMinutes, ResultGroupName, ResultStandard, ResultNotes, ResultUnit };
EditTarget edit_target = EditTarget::None;
int phrase_index = 0;
bool pinyin_mode = true;
char pinyin_input[32] = {};
int pinyin_page = 0;
bool templates_from_picker = false;
bool storage_ok = false, clock_ok = false, touch_down = false, backlight_off = false;
bool redraw = true, audio_ready = false, dimmed = false;
bool templates_ok = false, wake_only = false;
bool screen_locked = false;
PocketGuard unlock_guard;
uint32_t touch_begin_ms = 0;
uint8_t touch_samples = 0;
bool display_ready = false, screen_hidden_for_flash = false;
volatile uint32_t touch_irq_count = 0;
uint32_t last_touch_irq_count = 0;
uint32_t wake_reassert_ms = 0;
uint32_t touch_press_count = 0, wake_attempt_count = 0;
bool type_save_pending = false;
uint32_t type_save_due_ms = 0;
bool lab_save_pending = false;
uint32_t lab_save_due_ms = 0;
int touch_start_x = 0, touch_start_y = 0;
int touch_last_x = 0, touch_last_y = 0;
esp_codec_dev_handle_t play_device = nullptr;
float battery_voltage = 0;
uint16_t battery_raw = 0;
bool battery_valid = false;
// Coarse voltage bands only; this is not a measured state of charge.
int8_t battery_band = -1;
int brightness = 55;  // Percent visible brightness, inverted by the board controller.
int alarm_volume = 80;
nvs_handle_t sound_settings_handle = 0;
bool sound_settings_ready = false, sound_save_pending = false;
uint32_t last_input_ms = 0, last_draw_ms = 0;
uint32_t last_reminder_check_ms = 0;
int alert_run = -1, alert_step = -1;
uint32_t alert_end_ms = 0, alarm_sound_end_ms = 0, next_beep_ms = 0;
bool voice_sd_ready = false, voice_attempted = false, voice_playing = false, voice_failed = false;
File voice_file;
size_t voice_remaining = 0;
File voice_upload_file;
bool voice_upload_active = false;
uint32_t voice_upload_size = 0, voice_upload_received = 0, voice_upload_expected_crc = 0;
uint32_t voice_upload_crc = 0xffffffffU, voice_upload_last_ms = 0;
uint32_t voice_upload_last_ack_ms = 0;
char voice_upload_path[40] = {}, voice_upload_temp[44] = {};
int clock_field = 0;
datetime_t clock_edit{};
char toast[80] = {};
uint32_t toast_until_ms = 0;
struct HistoryRow {uint32_t id;bool archived;char sample[32];char project[24];int64_t created;};
HistoryRow history_rows[4]{};
uint32_t history_selected_id=0;
uint32_t history_shared_id=0;
bool history_selected_archived=false;
int history_date_filter=0;
char history_date_entry[9]={};
Sample history_sample{};Run history_run{};ResultRecord history_result{};
bool history_has_result=false;
bool history_has_comparison=false;
uint32_t history_comparison_baseline=0;
ComputedResult history_comparisons[kResultGroups]{};
int history_field=0;

void screen_before_flash() {
  screen_hidden_for_flash = display_ready && !backlight_off;
  if (screen_hidden_for_flash) {
    IO_EXTENSION_Pwm_Output(100);
    delay(10);  // Let the LEDs go dark before the cache-stalling flash write.
  }
}

void screen_after_flash() {
  if (!display_ready) return;
  waveshare_rgb_lcd_restart();
  if (screen_hidden_for_flash) {
    delay(65);  // Let the scheduled VSYNC restart and two scan periods finish.
    IO_EXTENSION_Pwm_Output(dimmed ? 85 : 100 - brightness);
    screen_hidden_for_flash = false;
  }
}

void schedule_type_save() {
  type_save_pending = true;
  type_save_due_ms = millis() + 3000;
}

void schedule_lab_save() {
  lab_save_pending = true;
  lab_save_due_ms = millis() + 3000;
}

void sound_settings_load() {
  if (nvs_open_from_partition("timer_nvs", "settings4c", NVS_READWRITE, &sound_settings_handle) != ESP_OK) return;
  sound_settings_ready = true;
  uint8_t stored = 0;
  if (nvs_get_u8(sound_settings_handle, "alarm_volume", &stored) == ESP_OK && stored <= 100)
    alarm_volume = stored;
}

bool sound_settings_save() {
  if (!sound_settings_ready) return false;
  screen_before_flash();
  esp_err_t result = nvs_set_u8(sound_settings_handle, "alarm_volume", static_cast<uint8_t>(alarm_volume));
  if (result == ESP_OK) result = nvs_commit(sound_settings_handle);
  screen_after_flash();
  return result == ESP_OK;
}

bool name_keyboard_active() {
  return page == Page::NewSample || page == Page::NewProject ||
         (page == Page::EditText && (edit_target == EditTarget::TemplateName ||
                                      edit_target == EditTarget::StepName || edit_target == EditTarget::ResultGroupName || edit_target == EditTarget::ResultStandard || edit_target == EditTarget::ResultNotes || edit_target == EditTarget::ResultUnit));
}

size_t entry_capacity() {
  return page == Page::NewSample || page == Page::NewProject || edit_target == EditTarget::ResultGroupName ? 23 : sizeof(entry) - 1;
}

void start_pinyin_name() {
  pinyin_mode = true;
  pinyin_input[0] = 0;
  pinyin_page = 0;
}

int pinyin_total_choices() {
  return pinyin_phrase_count(pinyin_phrase_choices(pinyin_input)) +
         pinyin_candidate_count(pinyin_candidates(pinyin_input));
}

bool pinyin_choice_at(int index, char out[32]) {
  const char* phrases = pinyin_phrase_choices(pinyin_input);
  const int phrase_count = pinyin_phrase_count(phrases);
  if (index < phrase_count) return pinyin_phrase_at(phrases, index, out);
  char character[5] = {};
  if (!pinyin_candidate_at(pinyin_candidates(pinyin_input), index - phrase_count, character)) return false;
  strcpy(out, character);
  return true;
}

bool save_type_now() {
  type_save_pending = false;
  return templates_save();
}

void notice(const char* message) {
  strncpy(toast, message, sizeof(toast) - 1);
  toast[sizeof(toast) - 1] = 0;
  toast_until_ms = millis() + 3000;
  redraw = true;
}

void box(int x, int y, int w, int h, uint16_t color) {
  if (!pixels) return;
  const int x0 = max(0, x), y0 = max(0, y), x1 = min(W, x + w), y1 = min(H, y + h);
  for (int yy = y0; yy < y1; ++yy) {
    uint16_t* row = pixels + yy * W;
    for (int xx = x0; xx < x1; ++xx) row[xx] = color;
  }
}

void label(int x, int y, const char* value, uint16_t color = WHITE_) {
  draw_text(pixels, W, x, y, value, color);
}

void label_fitted(int x, int y, int max_width, const char* value, uint16_t color = WHITE_) {
  if (text_width(value) <= max_width) { label(x, y, value, color); return; }
  char shortened[40] = {};
  size_t length = 0;
  for (const char* cursor = value; *cursor;) {
    const uint8_t first = static_cast<uint8_t>(*cursor);
    const size_t bytes = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2 :
                         (first & 0xF0) == 0xE0 ? 3 : (first & 0xF8) == 0xF0 ? 4 : 1;
    if (length + bytes + 4 > sizeof shortened || strlen(cursor) < bytes) break;
    memcpy(shortened + length, cursor, bytes);
    shortened[length + bytes] = 0;
    if (text_width(shortened) + text_width("...") > max_width) {
      shortened[length] = 0;
      break;
    }
    length += bytes;
    cursor += bytes;
  }
  strcat(shortened, "...");
  label(x, y, shortened, color);
}

void centered(int x, int y, int w, const char* value, uint16_t color = WHITE_) {
  label(x + max(0, (w - text_width(value)) / 2), y, value, color);
}

void hit(int x, int y, int w, int h, Action action, int arg = 0) {
  if (hit_count < static_cast<int>(sizeof(hits) / sizeof(hits[0])))
    hits[hit_count++] = {x, y, w, h, action, arg};
}

void button(int x, int y, int w, int h, const char* title, Action action,
            int arg = 0, uint16_t color = BLUE) {
  box(x, y, w, h, color);
  centered(x, y + (h - 24) / 2, w, title);
  hit(x, y, w, h, action, arg);
}

// Main 1-cell Li-ion battery: Waveshare's 10-bit ADC, 3.3 V reference, 3:1 divider.
// Voltage bands are only a rough guide, especially during charging or heavy load.
int8_t battery_band_for_voltage(float voltage, int8_t previous) {
  constexpr float cuts[] = {3.55f, 3.75f, 3.95f};
  int8_t band = previous < 0 ? 0 : previous;
  if (previous < 0) while (band < 3 && voltage >= cuts[band]) ++band;
  else {
    while (band < 3 && voltage >= cuts[band] + 0.05f) ++band;
    while (band > 0 && voltage < cuts[band - 1] - 0.05f) --band;
  }
  return band;
}

void poll_battery(uint32_t tick) {
  static uint32_t last_sample=0;
  static uint32_t sum=0;
  static int count=0, failures=0;
  if (static_cast<uint32_t>(tick-last_sample)<200) return;
  last_sample=tick;
  uint8_t reg=IO_EXTENSION_ADC_ADDR, bytes[2]={};
  const esp_err_t result=i2c_master_transmit_receive(IO_EXTENSION.addr,&reg,1,bytes,2,20);
  const uint16_t raw=static_cast<uint16_t>(bytes[0] | (bytes[1]<<8));
  const float voltage=raw*(3.0f*3.3f/1023.0f);
  if (result!=ESP_OK || raw>1023 || voltage<2.5f || voltage>4.35f) {
    sum=0; count=0;
    if (++failures>=3 && battery_valid) { battery_valid=false; battery_band=-1; redraw=true; }
    return;
  }
  failures=0;
  sum+=raw;
  if (++count<10) return;
  const bool had_value=battery_valid;
  const float previous_voltage=battery_voltage;
  battery_raw=static_cast<uint16_t>((sum+count/2)/count);
  const float measured=(sum/static_cast<float>(count))*(3.0f*3.3f/1023.0f);
  battery_voltage=battery_valid ? 0.75f*battery_voltage+0.25f*measured : measured;
  battery_valid=true;
  const int8_t previous_band=battery_band;
  battery_band=battery_band_for_voltage(battery_voltage,battery_band);
  if (battery_band==0 && previous_band>0) notice("电池电压偏低，请及时充电");
  sum=0; count=0;
  if (!backlight_off && (!had_value || fabsf(battery_voltage-previous_voltage)>=0.01f)) redraw=true;
  // ADC updates never wake the screen.
}

void draw_battery() {
  box(674,4,126,56,CARD);
  char volts[24];
  if(battery_valid)snprintf(volts,sizeof volts,"电池 %.2fV",battery_voltage);
  else snprintf(volts,sizeof volts,"电池 --.--V");
  label_fitted(680,7,116,volts,battery_valid?CYAN:MUTED);
  const char* bands[]={"估计 请充电","估计 偏低","估计 一般","估计 充足"};
  label_fitted(680,35,116,battery_valid?bands[battery_band]:"估计 未知",
               !battery_valid?MUTED:battery_band==0?ORANGE:battery_band==1?ORANGE:CYAN);
}

void title_bar(const char* title, const char* subtitle = nullptr) {
  box(0, 0, W, 64, CARD);
  label_fitted(20, 19, 300, title);
  if (subtitle) label_fitted(338, 20, 324, subtitle, MUTED);
  draw_battery();
}

void nav() {
  box(0, 424, W, 56, CARD);
  button(8, 428, 150, 48, "任务", Action::Home, 0, (page == Page::Dashboard || page == Page::ExportSelect || page == Page::ExportDates || page == Page::ExportSamples || page == Page::Transfer) ? CYAN : BLUE);
  button(166, 428, 150, 48, "样品", Action::Samples, 0, (page == Page::Samples || page == Page::SampleDates || page == Page::Sample || page == Page::SampleRecords || page == Page::StepRecord || page == Page::Results || page == Page::ResultOptions || page == Page::ResultStandards || page == Page::NumericPad || page == Page::ResultSummary || page == Page::HistoryList || page == Page::HistoryDetail || page == Page::HistoryDate || page == Page::HistoryField) ? CYAN : BLUE);
  button(324, 428, 150, 48, "试验类型", Action::OpenTemplates, 0,
         page == Page::Templates || page == Page::TemplateEdit ? CYAN : BLUE);
  button(482, 428, 150, 48, "设置", Action::OpenSettings, 0,
         page == Page::Settings || page == Page::Clock || page == Page::Stopwatch || page == Page::StopwatchRecords ? CYAN : BLUE);
  button(640, 428, 150, 48, "返回", Action::Back);
}

int64_t now_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

bool valid_time(const datetime_t& t) {
  return t.year >= 2024 && t.year <= 2069 && t.month >= 1 && t.month <= 12 &&
         t.day >= 1 && t.day <= 31 && t.hour < 24 && t.min < 60 && t.sec < 60;
}

void sync_from_rtc() {
  datetime_t rtc{};
  PCF85063A_Read_now(&rtc);
  clock_ok = valid_time(rtc);
  if (!clock_ok) return;
  tm parts{};
  parts.tm_year = rtc.year - 1900; parts.tm_mon = rtc.month - 1; parts.tm_mday = rtc.day;
  parts.tm_hour = rtc.hour; parts.tm_min = rtc.min; parts.tm_sec = rtc.sec;
  timeval tv{mktime(&parts), 0};
  settimeofday(&tv, nullptr);
}

void save_clock() {
  clock_edit.sec = 0;
  PCF85063A_Set_All(clock_edit);
  sync_from_rtc();
  redraw = true;
}

void format_clock(char* out, size_t size) {
  time_t raw = now_ms() / 1000;
  tm local{}; localtime_r(&raw, &local);
  snprintf(out, size, "%04d-%02d-%02d  %02d:%02d", local.tm_year + 1900,
           local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
}

void apply_brightness(int percent) {
  brightness = constrain(percent, 10, 100);
  IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);
  IO_EXTENSION_Pwm_Output(100 - brightness);
  backlight_off = false;
  dimmed = false;
}

void IRAM_ATTR touch_irq_handler() { ++touch_irq_count; }

void wake_display(uint32_t now) {
  ++wake_attempt_count;
  last_input_ms = now;
  apply_brightness(brightness);
  redraw = true;
  wake_reassert_ms = now + 300;
}

void lock_display() {
  screen_locked=true;
  unlock_guard.reset();
  wake_reassert_ms=0;
  touch_down=false;
  wake_only=true;
  IO_EXTENSION_Pwm_Output(100);
  backlight_off=true;
  Serial.println("SCREEN locked");
}
void power_tick(uint32_t now) {
  const uint32_t idle = now - last_input_ms;
  if (alert_run >= 0) { if (backlight_off) wake_display(now); return; }
  // Keep the stopwatch visible while running; manual locking remains effective.
  // Refresh the idle baseline so pausing grants a full 30-second idle period.
  if (stopwatch.running) { last_input_ms = now; return; }
  if (idle >= 30000 && !backlight_off) {
    // Keep the enable output asserted; extinguish the backlight through PWM.
    // After a long low period the board controller sometimes retained IO2 low
    // even after an acknowledged write requesting it high.
    lock_display();
  }
}

void audio_begin() {
  if (audio_ready) return;
  play_device = speaker_init();  // DAC only; do not initialize the microphone codec.
  audio_ready = play_device != nullptr;
  if (audio_ready) {
    speaker_audio_poweramp_enable(false);
  } else Serial.println("speaker_init failed");
}

bool audio_open(int volume) {
  if(record_audio_open){record_audio_open=false;record_tone_remaining=0;esp_codec_dev_close(play_device);speaker_audio_poweramp_enable(false);}
  audio_begin();
  if (!audio_ready) return false;
  esp_codec_dev_sample_info_t sample_info{};
  sample_info.bits_per_sample = 16;
  sample_info.channel = 2;
  sample_info.sample_rate = 44100;
  const int opened = esp_codec_dev_open(play_device, &sample_info);
  if (opened != ESP_OK) { Serial.printf("speaker open failed: %d\n", opened); return false; }
  // Preserve the normal range; give the upper end a modest 3 dB boost.
  // The ES8311 has hardware gain headroom, but higher levels may distort a small speaker.
  esp_codec_dev_vol_map_t points[] = {{0, -50.0f}, {80, -10.0f}, {100, 3.0f}};
  esp_codec_dev_vol_curve_t curve{points, 3};
  const int curve_result = esp_codec_dev_set_vol_curve(play_device, &curve);
  if (curve_result != ESP_OK) Serial.printf("speaker volume curve failed: %d\n", curve_result);
  const int volume_result = esp_codec_dev_set_out_vol(play_device, volume);
  if (volume_result != ESP_OK) Serial.printf("speaker volume failed: %d\n", volume_result);
  speaker_audio_poweramp_enable(true);
  delay(20);  // Let the external amplifier settle before feeding PCM.
  return true;
}

void audio_close() {
  delay(80);  // Drain the I2S DMA queue before muting the amplifier.
  esp_codec_dev_close(play_device);
  speaker_audio_poweramp_enable(false);
}

uint64_t voice_key(const char* project, const char* step) {
  uint64_t hash = 14695981039346656037ULL;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(project); *p; ++p) { hash ^= *p; hash *= 1099511628211ULL; }
  hash ^= 0; hash *= 1099511628211ULL;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(step); *p; ++p) { hash ^= *p; hash *= 1099511628211ULL; }
  return hash;
}

bool voice_start(const Run& run, const Step& step) {
  if (!voice_sd_ready) { Serial.println("VOICE_PLAY no_card"); return false; }
  char path[40];
  snprintf(path, sizeof path, "/voice/%016llx.pcm", static_cast<unsigned long long>(voice_key(run.name, step.name)));
  voice_file = SD_MMC.open(path, FILE_READ);
  if (!voice_file) { Serial.printf("VOICE_PLAY missing %s\n", path); return false; }
  const size_t size = voice_file.size();
  if (size == 0 || size > 44100 * 4 * 48 / 10 || (size % 4) != 0) {
    Serial.printf("VOICE_PLAY invalid_size %u\n", unsigned(size)); voice_file.close();
    return false;
  }
  if (alarm_volume == 0) { Serial.println("VOICE_PLAY muted"); voice_file.close(); return true; }
  if (!audio_open(alarm_volume)) { Serial.println("VOICE_PLAY audio_open_failed"); voice_file.close(); return false; }
  voice_remaining = size;
  voice_playing = true;
  Serial.printf("VOICE_PLAY started %s bytes=%u volume=%d\n", path, unsigned(size), alarm_volume);
  return true;
}

void voice_stop() {
  if (!voice_playing) return;
  voice_playing = false;
  voice_remaining = 0;
  voice_file.close();
  audio_close();
  Serial.println("VOICE_PLAY stopped");
}

void voice_tick() {
  if (!voice_playing) return;
  static int16_t buffer[1024];
  const size_t n = voice_file.read(reinterpret_cast<uint8_t*>(buffer), sizeof buffer);
  if (n == 0 && voice_remaining == 0) { voice_stop(); return; }
  if (n == 0 || n > voice_remaining) {
    Serial.println("voice file ended early");
    voice_stop(); voice_failed = true; next_beep_ms = millis(); return;
  }
  if ((n % 4) != 0 || esp_codec_dev_write(play_device, buffer, n) != ESP_OK) {
    Serial.println("voice playback failed");
    voice_stop();
    voice_failed = true;
    voice_attempted = true;
    next_beep_ms = millis();
    return;
  }
  voice_remaining -= n;
}

void beep(int ms, int frequency = 880) {
  if (!audio_open(85)) return;
  int16_t samples[256 * 2];
  const int total = 44100 * ms / 1000;
  for (int offset = 0; offset < total; offset += 256) {
    const int count = min(256, total - offset);
    for (int i = 0; i < count; ++i) {
      const int phase = ((offset + i) * frequency * 2 / 44100) & 1;
      const int16_t value = phase ? 16000 : -16000;
      samples[i * 2] = value; samples[i * 2 + 1] = value;
    }
    const int result = esp_codec_dev_write(play_device, samples, count * 4);
    if (result != ESP_OK) { Serial.printf("speaker write failed: %d\n", result); break; }
  }
  audio_close();
}

// Feed the short acknowledgement in small chunks, without beep()'s blocking drain.
void record_feedback_tick(uint32_t tick) {
  const bool wanted=((page==Page::Stopwatch&&!screen_locked&&!backlight_off)||(record_audio_test_until&&int32_t(tick-record_audio_test_until)<0))&&alert_run<0&&!voice_playing&&!voice_upload_active;
  if(!wanted){
    record_tone_remaining=0;
    if(record_audio_open){record_audio_open=false;record_tone_remaining=0;esp_codec_dev_close(play_device);speaker_audio_poweramp_enable(false);}
    return;
  }
  if(!record_audio_open){
    static uint32_t retry_at=0;if(int32_t(tick-retry_at)<0)return;
    if(!audio_open(65)){retry_at=tick+3000;return;}
    retry_at=0;
    record_audio_open=true;
    int16_t silent[128*2]={};esp_codec_dev_write(play_device,silent,sizeof silent);
    speaker_audio_poweramp_enable(false);
  }
  if(record_tone_remaining>0){
    if(record_tone_phase==0)speaker_audio_poweramp_enable(true);
    int16_t samples[128*2];const int n=min(128,record_tone_remaining);
    for(int i=0;i<n;++i){const int16_t v=((record_tone_phase++*2000*2/44100)&1)?6000:-6000;samples[2*i]=samples[2*i+1]=v;}
    if(esp_codec_dev_write(play_device,samples,n*4)!=ESP_OK){record_tone_remaining=0;Serial.println("RECORD_BEEP failed");}
    else record_tone_remaining-=n;
    record_audio_mute_at=millis()+80;
    if(!record_tone_remaining){int16_t silent[128*2]={};esp_codec_dev_write(play_device,silent,sizeof silent);Serial.println("RECORD_BEEP queued");}
  }else if(record_audio_mute_at&&int32_t(tick-record_audio_mute_at)>=0){
    record_audio_mute_at=0;speaker_audio_poweramp_enable(false);
  }
}

void alarm_melody() {
  if (alarm_volume == 0 || !audio_open(alarm_volume)) return;
  static int16_t sine[256];
  static bool sine_ready = false;
  if (!sine_ready) {
    for (int i = 0; i < 256; ++i)
      sine[i] = static_cast<int16_t>(sinf(i * 6.2831853f / 256.0f) * 14000);
    sine_ready = true;
  }
  struct Note { int frequency; int duration_ms; };
  constexpr Note notes[] = {{523, 155}, {0, 40}, {659, 155}, {0, 40}, {784, 260}};
  int16_t samples[256 * 2];
  bool okay = true;
  for (const Note& note : notes) {
    const int total = 44100 * note.duration_ms / 1000;
    uint32_t phase = 0;
    const uint32_t phase_step = static_cast<uint32_t>((static_cast<uint64_t>(note.frequency) << 32) / 44100);
    for (int offset = 0; offset < total && okay; offset += 256) {
      const int count = min(256, total - offset);
      for (int i = 0; i < count; ++i) {
        const int position = offset + i;
        const int fade_in = min(256, position * 256 / 441);
        const int fade_out = min(256, (total - position) * 256 / 662);
        const int fade = min(fade_in, fade_out);
        const int16_t value = note.frequency ? sine[phase >> 24] * fade / 256 : 0;
        samples[i * 2] = value;
        samples[i * 2 + 1] = value;
        phase += phase_step;
      }
      const int result = esp_codec_dev_write(play_device, samples, count * 4);
      if (result != ESP_OK) { Serial.printf("speaker write failed: %d\n", result); okay = false; }
    }
    if (!okay) break;
  }
  audio_close();
}

int active_step(int run_index) {
  return run_index >= 0 && run_index < lab.run_count ? current_index(lab.runs[run_index]) : -1;
}

void countdown(const Step& step, int64_t now, char* out, size_t size) {
  if (!step.started) { snprintf(out, size, "待开始"); return; }
  if (step_finished(step)) { snprintf(out, size, "已结束"); return; }
  if (step.minutes <= 0) { snprintf(out, size, "手动步骤"); return; }
  const int64_t left = remaining_ms(step, now);
  char duration[36]; format_duration(left, duration, sizeof duration);
  snprintf(out, size, "%s %s", left > 0 ? "剩余" : "已到点", duration);
}

void task_lists(int* active, int& active_count, int* due, int& due_count) {
  active_count = due_count = 0;
  const int64_t at = now_ms();
  for (int i = 0; i < lab.run_count; ++i) {
    const int j = current_index(lab.runs[i]);
    if (j < 0) continue;
    const Step& step = lab.runs[i].steps[j];
    if (step.minutes <= 0) continue;
    int* dest = due_ms(step) <= at ? due : active;
    int& count = due_ms(step) <= at ? due_count : active_count;
    int pos = count++;
    while (pos > 0 && due_ms(lab.runs[dest[pos - 1]].steps[active_step(dest[pos - 1])]) > due_ms(step)) {
      dest[pos] = dest[pos - 1]; --pos;
    }
    dest[pos] = i;
  }
}

void page_control(int index,int total,int x,int y,int width,int height) {
  char text[64];snprintf(text,sizeof text,"第 %d/%d 页 · 跳转",index+1,total);
  button(x,y,width,height,text,Action::JumpOpen,total,BLUE);
}
void draw_page_jump() {
  char text[64];snprintf(text,sizeof text,"输入页码：1 至 %d",jump_total);title_bar("跳转到指定页",text);
  box(18,73,764,48,CARD);label(35,84,jump_entry,CYAN);
  for(int i=0;i<10;++i){const int col=i==9?1:i%3,row=i==9?3:i/3;char key[2]={char(i==9?'0':'1'+i),0};button(18+col*182,132+row*67,170,58,key,Action::JumpKey,key[0],KEY_BG);}
  button(575,132,207,58,"退格",Action::JumpKey,'<');button(575,199,207,58,"清空",Action::JumpKey,'C');
  button(575,266,207,58,"跳转",Action::JumpGo,0,GREEN);button(575,333,207,58,"返回",Action::Back);nav();
}

void draw_dashboard() {
  int active[kMaxRuns], due[kMaxRuns], na = 0, nd = 0;
  task_lists(active, na, due, nd);
  char clock_text[40]; format_clock(clock_text, sizeof clock_text);
  title_bar("当前任务");
  label_fitted(210,20,270,clock_ok?clock_text:"时钟未设置",MUTED);
  button(490,9,172,46,"导出／传输",Action::ExportOpen,0,BLUE);
  char t0[32], t1[32];
  snprintf(t0, sizeof t0, "计时中 %d", na); snprintf(t1, sizeof t1, "到点待确认 %d", nd);
  button(18, 76, 190, 45, t0, Action::TaskTab, 0, task_tab == 0 ? CYAN : BLUE);
  button(216, 76, 218, 45, t1, Action::TaskTab, 1, task_tab == 1 ? RED : BLUE);
  const int count = task_tab == 0 ? na : nd;
  int* selected = task_tab == 0 ? active : due;
  if (count == 0) {
    label(28, 165, task_tab == 0 ? "没有正在计时的项目" : "没有到点待确认的项目", MUTED);
    button(28, 235, 300, 80, "查看样品和项目", Action::Samples, 0, BLUE);
    button(346, 235, 300, 80, "+ 建立新样品", Action::NewSample, 0, GREEN);
  }
  const int start = list_page * 3;
  for (int row = 0; row < 3 && start + row < count; ++row) {
    const int index = selected[start + row];
    const Run& run = lab.runs[index];
    const int si = lab_find_sample(run.sample_id);
    const Step& step = run.steps[active_step(index)];
    const int y = 132 + row * 91;
    box(18, y, 764, 82, CARD);
    label(30, y + 9, si >= 0 ? lab.samples[si].name : run.sample, WHITE_);
    char line[80]; snprintf(line, sizeof line, "%s · %s · %s", phase_label(run.phase), run.name, step.name);
    label_fitted(30, y + 43, 590, line, MUTED);
    char timer[60]; countdown(step, now_ms(), timer, sizeof timer);
    label(465, y + 9, timer, task_tab == 0 ? CYAN : ORANGE);
    button(636, y + 42, 130, 32, task_tab == 0 ? "查看" : "确认完成", Action::OpenRun, index, GREEN);
  }
  page_control(list_page,max(1,(count+2)/3),455,76,327,45);
  nav();
}

int sample_date_key(const Sample& sample) {
  if (sample.created<=0) return 0;
  time_t raw=static_cast<time_t>(sample.created/1000);
  tm date{}; localtime_r(&raw,&date);
  const int year=date.tm_year+1900;
  if (year<2024 || year>2069) return 0;
  return year*10000+(date.tm_mon+1)*100+date.tm_mday;
}

void date_label(int key, char* out, size_t size) {
  if (key<=0) snprintf(out,size,"日期未设置");
  else snprintf(out,size,"%04d-%02d-%02d",key/10000,key/100%100,key%100);
}

int sample_dates(int* keys) {
  int count=0;
  for (int i=0;i<lab.sample_count;++i) {
    const int key=sample_date_key(lab.samples[i]);
    bool exists=false;
    for (int j=0;j<count;++j) if (keys[j]==key) exists=true;
    if (!exists) keys[count++]=key;
  }
  for (int i=0;i<count;++i) for (int j=i+1;j<count;++j)
    if (keys[j]>keys[i]) { const int tmp=keys[i];keys[i]=keys[j];keys[j]=tmp; }
  return count;
}

int samples_on_date(int key) {
  int count=0;
  for (int i=0;i<lab.sample_count;++i) if (key<0 || sample_date_key(lab.samples[i])==key) ++count;
  return count;
}

bool history_is_past(const Run& run,int64_t now) {
  const int64_t created=run_age_get(run.id);
  return clock_ok&&history_age_reached(created,now,run_age_hold_until(run.id),3)&&!history_run_busy(run,now);
}
void history_auto_tick(uint32_t tick) {
  static uint32_t last_check=0;
  if(tick-last_check<60000)return;
  last_check=tick;
  if(!clock_ok||!screen_locked||!records_ok()||alert_run>=0||voice_playing||voice_upload_active||
     transfer_active()||confirm_kind!=ConfirmKind::None||lab_save_pending||type_save_pending)return;
  if(page==Page::ExportSelect||page==Page::ExportDates||page==Page::ExportSamples||page==Page::Transfer)return;
  const int64_t now=now_ms();
  for(int i=0;i<lab.run_count;++i)for(int j=0;j<lab.runs[i].count;++j) {
    const Step& s=lab.runs[i].steps[j];
    if(s.started&&!s.done&&!s.skipped && s.minutes>0 && due_ms(s)>now && due_ms(s)-now<60000)return;
  }
  uint32_t protected_run=0;
  if(selected_run>=0&&selected_run<lab.run_count&&(page==Page::Run||page==Page::StepRecord||page==Page::Results||page==Page::ResultOptions||page==Page::ResultStandards||page==Page::NumericPad))protected_run=lab.runs[selected_run].id;
  if(page==Page::HistoryDetail&&!history_selected_archived)protected_run=history_selected_id;
  const int i=history_find_candidate(now,7,0,protected_run);
  if(i>=0) {
    const Run& run=lab.runs[i];
    const uint32_t sample_id=selected_sample>=0&&selected_sample<lab.sample_count?lab.samples[selected_sample].id:0;
    const uint32_t run_id=selected_run>=0&&selected_run<lab.run_count?lab.runs[selected_run].id:0;
    const uint32_t moved=run.id;
    const char* error=history_archive_run(i,now);
    selected_sample=sample_id?lab_find_sample(sample_id):-1;
    selected_run=run_id?lab_find_run(run_id):-1;
    if(error)Serial.printf("HISTORY_AUTO failed id=%u %s\n",moved,error);
    else {Serial.printf("HISTORY_AUTO archived id=%u\n",moved);redraw=true;}
    return; // One project at a time, keeping timer and UI service responsive.
  }
}
bool history_make_room(bool sample_slot,uint32_t keep_sample) {
  if(!clock_ok||!records_ok()||alert_run>=0||voice_playing||voice_upload_active||transfer_active())return false;
  const int64_t now=now_ms();
  for(int i=0;i<lab.run_count;++i)for(int j=0;j<lab.runs[i].count;++j){const Step& s=lab.runs[i].steps[j];
    if(s.started&&!s.done&&!s.skipped&&s.minutes>0&&due_ms(s)>now&&due_ms(s)-now<60000)return false;}
  for(int attempts=0;attempts<kMaxRuns;++attempts) {
    if(sample_slot?lab.sample_count<kMaxSamples:lab.run_count<kMaxRuns)return true;
    const int index=history_find_candidate(now,3);
    if(index<0)return false;
    const uint32_t current_sample=selected_sample>=0&&selected_sample<lab.sample_count?lab.samples[selected_sample].id:0;
    const uint32_t current_run=selected_run>=0&&selected_run<lab.run_count?lab.runs[selected_run].id:0;
    const bool keep=keep_sample&&lab.runs[index].sample_id==keep_sample;
    const char* error=history_archive_run(index,now,keep);
    selected_sample=current_sample?lab_find_sample(current_sample):-1;
    selected_run=current_run?lab_find_run(current_run):-1;
    if(error){Serial.printf("HISTORY_CAPACITY failed %s\n",error);return false;}
    Serial.println("HISTORY_CAPACITY archived");
  }
  return sample_slot?lab.sample_count<kMaxSamples:lab.run_count<kMaxRuns;
}
int history_date_key(int64_t ms) {
  if(ms<=0)return 0;
  time_t sec=ms/1000;struct tm t{};localtime_r(&sec,&t);
  return (t.tm_year+1900)*10000+(t.tm_mon+1)*100+t.tm_mday;
}

void draw_sample_dates() {
  char clock_text[40]; format_clock(clock_text,sizeof clock_text);
  title_bar("样品记录",clock_ok ? clock_text : "时钟未设置");
  button(18,75,220,47,"往期历史",Action::HistoryOpen,0,BLUE);
  button(575,75,207,47,"+ 建立样品",Action::NewSample,0,GREEN);
  int keys[kMaxSamples]={};
  const int count=sample_dates(keys);
  list_page=constrain(list_page,0,max(0,(count-1)/4));
  const int start=list_page*4;
  for (int row=0;row<4 && start+row<count;++row) {
    const int key=keys[start+row], y=132+row*69;
    int runs=0;
    for (int i=0;i<lab.sample_count;++i)
      if (sample_date_key(lab.samples[i])==key) runs+=lab_runs_for_sample(lab.samples[i].id);
    box(18,y,764,62,BLUE);
    char name[32],info[64];date_label(key,name,sizeof name);
    label(30,y+7,name);
    snprintf(info,sizeof info,"%d 个样品 · %d 项实验",samples_on_date(key),runs);
    label(30,y+34,info,MUTED);
    button(635,y+11,130,40,"查看",Action::OpenDate,key,GREEN);
    hit(18,y,600,62,Action::OpenDate,key);
  }
  if (!count) label(28,180,"还没有样品，先建立样品编号",MUTED);
  page_control(list_page,max(1,(count+3)/4),250,75,310,46);
  nav();
}

void draw_samples() {
  char heading[40];date_label(selected_date,heading,sizeof heading);
  title_bar(heading,"当天样品");
  button(575,75,207,47,"+ 建立样品",Action::NewSample,0,GREEN);
  const int count=samples_on_date(selected_date);
  list_page=constrain(list_page,0,max(0,(count-1)/4));
  const int start=list_page*4;
  int found=0,shown=0;
  for (int i=lab.sample_count-1;i>=0 && shown<4;--i) {
    const Sample& sample=lab.samples[i];
    if (selected_date>=0 && sample_date_key(sample)!=selected_date) continue;
    if (found++<start) continue;
    const int y=134+shown++*69;
    box(18,y,764,62,BLUE);
    label(30,y+9,sample.name);
    char info[50];snprintf(info,sizeof info,"%d 项实验",lab_runs_for_sample(sample.id));
    label(30,y+35,info,MUTED);
    button(635,y+11,130,40,"打开",Action::OpenSample,i,GREEN);
    hit(18,y,600,62,Action::OpenSample,i);
  }
  if (!count) label(28,180,"当天暂无样品",MUTED);
  page_control(list_page,max(1,(count+3)/4),18,75,300,46);
  nav();
}

void draw_sample() {
  if (selected_sample < 0 || selected_sample >= lab.sample_count) { page = Page::Samples; draw_samples(); return; }
  const Sample& sample = lab.samples[selected_sample];
  char info[55]; snprintf(info, sizeof info, "%d 项实验", lab_runs_for_sample(sample.id));
  title_bar(sample.name, info);
  button(18,75,180,46,"+ 添加项目",Action::PickProject,0,GREEN);
  button(208,75,180,46,"试验记录",Action::SampleRecords,0,BLUE);
  button(398,75,180,46,"结果汇总",Action::ResultSummary,0,BLUE);

  button(515,9,126,46,"删除样品",Action::DeleteSample,0,RED);
  const int start = list_page * 4;
  int found = 0, shown = 0;
  for (int i = lab.run_count - 1; i >= 0; --i) {
    const Run& run = lab.runs[i];
    if (run.sample_id != sample.id) continue;
    if (history_is_past(run,now_ms())) continue;
    if (found++ < start) continue;
    if (shown >= 4) continue;
    const int y = 132 + shown++ * 70;
    box(18, y, 764, 63, BLUE);
    char run_title[96]; snprintf(run_title,sizeof run_title,"%s · %s",phase_label(run.phase),run.name);
    label_fitted(30, y + 7, 440, run_title);
    const int si = current_index(run);
    char status[70];
    if (si >= 0) countdown(run.steps[si], now_ms(), status, sizeof status);
    else status_text(run, status, sizeof status);
    label(30, y + 36, status, si >= 0 ? CYAN : MUTED);
    button(490,y+11,130,41,"录入数据",Action::ResultOpen,i,BLUE);
    button(635, y + 11, 130, 41, "步骤", Action::OpenRun, i, GREEN);
    hit(18, y, 450, 63, Action::OpenRun, i);
  }
  if (!found) label(28, 181, lab_runs_for_sample(sample.id)?"项目已移入往期历史":"样品尚无实验项目", MUTED);
  page_control(list_page,max(1,(found+3)/4),588,75,194,46);
  nav();
}

void draw_history_list() {
  title_bar("往期历史",history_date_filter?"按项目建立日期":"板内和TF卡项目");
  button(18,75,168,46,"选择日期",Action::HistoryDateOpen,0,BLUE);
  button(196,75,125,46,"全部",Action::HistoryAll,0,BLUE);
  const int64_t now=now_ms();int live=0;
  for(int i=0;i<lab.run_count;++i)if(history_is_past(lab.runs[i],now) &&
     (!history_date_filter||history_date_key(run_age_get(lab.runs[i].id))==history_date_filter))++live;
  const int archived=history_list(0,nullptr,0,history_date_filter);
  const int count=live+max(0,archived);
  list_page=constrain(list_page,0,max(0,(count-1)/4));
  const int start=list_page*4;int ordinal=0,shown=0;memset(history_rows,0,sizeof history_rows);
  for(int i=lab.run_count-1;i>=0&&shown<4;--i) {
    const Run& run=lab.runs[i];int64_t created=run_age_get(run.id);
    if(!history_is_past(run,now)||(history_date_filter&&history_date_key(created)!=history_date_filter))continue;
    if(ordinal++<start)continue;
    HistoryRow& row=history_rows[shown++];row.id=run.id;row.created=created;row.archived=false;
    snprintf(row.sample,sizeof row.sample,"%s",run.sample);snprintf(row.project,sizeof row.project,"%s",run.name);
  }
  if(shown<4&&archived>0) {
    HistoryItem items[4]{};int offset=max(0,start-live);
    const int got=history_list(offset,items,4-shown,history_date_filter);
    const int available=got<0?0:min(4-shown,max(0,got-offset));
    for(int i=0;i<available;++i){HistoryRow& row=history_rows[shown++];row.id=items[i].id;row.created=items[i].created;row.archived=true;
      memcpy(row.sample,items[i].sample,sizeof row.sample);memcpy(row.project,items[i].project,sizeof row.project);}
  }
  for(int i=0;i<shown;++i){const HistoryRow& row=history_rows[i];const int y=132+i*69;box(18,y,764,62,BLUE);
    char text[90],date[32];record_time(row.created,date,sizeof date);date[10]=0;
    snprintf(text,sizeof text,"%s · %s",row.sample,row.project);label_fitted(30,y+5,520,text);
    snprintf(text,sizeof text,"%s · %s",row.created?date:"日期未知",row.archived?"TF卡":"板内");label(30,y+34,text,MUTED);
    button(590,y+8,176,46,"导出手机",Action::HistoryExportRow,i,GREEN);hit(18,y,560,62,Action::HistoryItemOpen,i);}
  if(!count)label(28,180,archived<0?"TF卡不可用":"暂无往期项目",MUTED);
  page_control(list_page,max(1,(count+3)/4),330,75,240,46);nav();
}

void draw_history_date() {
  title_bar("选择历史日期","输入年月日，例如 20261102");
  box(24,72,750,48,CARD);label(36,82,history_date_entry[0]?history_date_entry:"请输入八位日期",CYAN);
  const char* keys="123456789C0<";
  for(int i=0;i<12;++i){int col=i%3,row=i/3;char text[4]={keys[i],0};
    if(keys[i]=='C')strcpy(text,"清");else if(keys[i]=='<')strcpy(text,"删");
    button(80+col*155,130+row*67,140,57,text,Action::HistoryDateKey,keys[i],KEY_BG);}
  button(570,151,190,90,"查找",Action::HistoryDateApply,0,GREEN);
  button(570,265,190,90,"全部日期",Action::HistoryAll,0,BLUE);nav();
}

void draw_history_detail() {
  char heading[80];snprintf(heading,sizeof heading,"%s · %s",history_sample.name,history_run.name);title_bar(heading);
  button(505,9,160,46,history_selected_archived?"恢复项目":"打开项目",Action::HistoryRestore,0,GREEN);
  button(495,75,285,46,"导出到手机",Action::HistoryExport,0,BLUE);
  if(!history_selected_archived){int index=lab_find_run(history_selected_id);if(index>=0)button(288,75,192,46,"录入数据",Action::ResultOpen,index,BLUE);}
  const int groups=history_has_result?history_result.group_count:0;
  const int total=history_run.count+groups;
  list_page=constrain(list_page,0,max(0,(total-1)/4));
  page_control(list_page,max(1,(total+3)/4),18,75,260,46);
  for(int row=0;row<4&&list_page*4+row<total;++row){int i=list_page*4+row,y=132+row*69;box(18,y,764,62,BLUE);
    char line[150];
    if(i<history_run.count){const Step& s=history_run.steps[i];
      snprintf(line,sizeof line,"%s · %d 分钟 · %s",s.name,s.minutes,s.done?"已完成":s.skipped?"已跳过":s.started?"已开始":"待开始");
      label_fitted(30,y+5,730,line);char date[32];record_time(s.started,date,sizeof date);label_fitted(30,y+34,730,date,MUTED);
    }else {const auto& group=history_result.groups[i-history_run.count];auto value=calculate_result(history_result,i-history_run.count);
      snprintf(line,sizeof line,"%s · %s %s",group.name,value.valid?value.text:"未完成",value.unit);label_fitted(30,y+5,730,line);
      String readings;for(int k=0;k<group.count&&k<kReadings;++k){if(k)readings+=" / ";readings+=group.readings[k][0]?group.readings[k]:"--";}
      label_fitted(30,y+34,730,readings.c_str(),MUTED);}
    hit(18,y,764,62,Action::HistoryFieldOpen,i);}
  nav();
}

void draw_history_field() {
  title_bar("历史详情",history_run.name);
  char line[160],date[32];
  if(history_field<history_run.count){const Step& s=history_run.steps[history_field];
    snprintf(line,sizeof line,"%s · %d 分钟 · %s",s.name,s.minutes,mode_label(s.mode));label_fitted(28,82,740,line,CYAN);
    const int64_t times[]={s.started,s.started&&s.minutes>0?due_ms(s):0,s.done,s.skipped};
    const char* names[]={"开始","计划到点","确认完成","跳过"};
    for(int i=0;i<4;++i){record_time(times[i],date,sizeof date);snprintf(line,sizeof line,"%s：%s",names[i],date);label_fitted(28,142+i*55,740,line);}
  }else if(history_has_result&&history_field-history_run.count<history_result.group_count){
    int group_index=history_field-history_run.count;const auto& group=history_result.groups[group_index];
    snprintf(line,sizeof line,"%s · %s",group.name,group.segregation?"离析":"普通");label_fitted(28,82,740,line,CYAN);
    for(int i=0;i<group.count&&i<kReadings;++i){snprintf(line,sizeof line,"读数%d：%s",i+1,group.readings[i][0]?group.readings[i]:"--");label_fitted(28+(i%2)*380,135+(i/2)*52,360,line);}
    const auto value=calculate_result(history_result,group_index);
    snprintf(line,sizeof line,"结果：%s %s",value.valid?value.text:"未完成",value.unit);label_fitted(28,310,740,line,CYAN);
    if(history_result.metric==Metric::Mass){snprintf(line,sizeof line,"质量修正：%s",group.correction[0]?group.correction:"0");label_fitted(28,362,740,line,MUTED);}
    else if(history_has_comparison){const auto& c=history_comparisons[group_index];
      snprintf(line,sizeof line,"老化比较：%s %s",c.valid?c.text:c.reason,c.unit);label_fitted(28,362,740,line,MUTED);}
  }
  nav();
}

void draw_sample_records() {
  if (selected_sample<0 || selected_sample>=lab.sample_count) {page=Page::SampleDates;draw_sample_dates();return;}
  const Sample& sample=lab.samples[selected_sample];
  title_bar("试验记录",sample.name);
  label(22,77,records_ok()?"TF 已保存 · 记录时间为北京时间":"TF 未同步 · 板内记录仍可查看",records_ok()?GREEN:ORANGE);
  int found=0,shown=0;const int start=list_page*4;
  for(int i=lab.run_count-1;i>=0;--i) {
    const Run& run=lab.runs[i];if(run.sample_id!=sample.id)continue;
    if(found++<start)continue;
    if(shown>=4)continue;
    const int y=112+shown++*65;
    box(18,y,764,59,CARD);
    char name[100];snprintf(name,sizeof name,"%s · %s",phase_label(run.phase),run.name);
    label_fitted(30,y+5,590,name);
    char text[64];status_text(run,text,sizeof text);label(30,y+31,text,MUTED);
    button(635,y+8,130,43,"查看",Action::OpenRecord,i,GREEN);
  }
  if(!found)label(26,165,"样品尚无实验项目",MUTED);
  page_control(list_page,max(1,(found+3)/4),474,376,308,38);
  nav();
}

void draw_step_record() {
  if(selected_run<0 || selected_run>=lab.run_count) {page=Page::SampleRecords;draw_sample_records();return;}
  const Run& run=lab.runs[selected_run];
  selected_step=constrain(selected_step,0,run.count-1);
  const Step& step=run.steps[selected_step];
  title_bar("试验记录",run.sample);
  char name[100];snprintf(name,sizeof name,"%s · %s",phase_label(run.phase),run.name);
  label_fitted(22,75,750,name,CYAN);
  for(int i=0;i<run.count;++i) {
    const int y=112+i*48;
    button(18,y,270,43,run.steps[i].name,Action::SelectStep,i,i==selected_step?CYAN:BLUE);
  }
  box(300,110,482,298,CARD);
  label_fitted(314,119,450,step.name);
  const char* state=step.done?"已确认完成":step.skipped?"已跳过":step.started?"进行中":"待开始";
  label(314,150,state,ORANGE);
  const int64_t times[]={step.started,step.started && step.minutes>0?due_ms(step):0,step.done,step.skipped};
  const char* labels[]={"开始", "到点", "完成", "跳过"};
  for(int i=0;i<4;++i) {
    char text[32];record_time(times[i],text,sizeof text);
    label(314,184+i*32,labels[i],MUTED);label(372,184+i*32,text);
  }
  char elapsed[80];const int64_t ended=step.done?step.done:step.skipped;
  if(step.started && ended>=step.started) {
    char duration[40];format_duration(ended-step.started,duration,sizeof duration);
    snprintf(elapsed,sizeof elapsed,"实际用时 %s",duration);
  } else snprintf(elapsed,sizeof elapsed,"计划 %d 分钟",step.minutes);
  label(314,322,elapsed,CYAN);
  label(314,363,"完成时间以手动确认为准",MUTED);
  nav();
}

void draw_run() {
  if (selected_run < 0 || selected_run >= lab.run_count) { page = Page::Samples; draw_samples(); return; }
  const Run& run = lab.runs[selected_run];
  selected_step = constrain(selected_step, 0, run.count - 1);
  const Step& step = run.steps[selected_step];
  title_bar(run.name);
  button(326, 9, 156, 46, phase_label(run.phase), Action::RunPhase, 0, BLUE);
  button(490, 9, 154, 46, "删除项目", Action::DeleteRun, 0, RED);
  for (int i = 0; i < run.count; ++i) {
    const int y = 79 + i * 55;
    box(18, y, 291, 51, i == selected_step ? CYAN : BLUE);
    char name[56]; snprintf(name, sizeof name, "%d. %s", i + 1, run.steps[i].name);
    label(28, y + 3, name, WHITE_);
    char status[50]; countdown(run.steps[i], now_ms(), status, sizeof status);
    if (run.steps[i].done) strcpy(status, "已完成");
    else if (run.steps[i].skipped) strcpy(status, "已跳过");
    label(28, y + 27, status, i == selected_step ? WHITE_ : MUTED);
    hit(18, y, 291, 51, Action::SelectStep, i);
  }
  box(322, 74, 460, 339, CARD);
  label_fitted(338, 83, 240, run.sample, MUTED);
  label(338, 117, step.name);
  char status[64]; countdown(step, now_ms(), status, sizeof status);
  if (step.done) strcpy(status, "已确认完成");
  else if (step.skipped) strcpy(status, "已跳过");
  label(338, 158, status, step.started && !step_finished(step) ? CYAN : ORANGE);
  char detail[64]; snprintf(detail, sizeof detail, "计划 %d 分钟 · %s", step.minutes, mode_label(step.mode));
  if (!step_finished(step))
    button(338, 190, 428, 44, detail, Action::RunMinutes, selected_step, BLUE);
  else label(338, 196, detail, MUTED);
  if (!step.started && !step_finished(step)) {
    button(338, 239, 428, 62, "切换提醒方式", Action::StepMode, selected_step);
  }
  if (!step_finished(step)) {
    button(338, 318, 428, 80, step.started ? "确认完成本步" : "开始本步",
           Action::StepAction, selected_step, step.started ? BLUE : GREEN);
  }
  nav();
}

void draw_templates(bool picker) {
  title_bar(picker ? "选择试验类型" : "试验类型设置",
            picker && selected_sample >= 0 ? lab.samples[selected_sample].name : nullptr);
  if (picker) {
    for (int i=0;i<3;++i)
      button(18+i*166,74,158,48,phase_label(static_cast<AgingPhase>(i)),Action::ProjectPhase,i,
             static_cast<int>(project_phase)==i ? GREEN : BLUE);
  }
  button(548, 74, 234, 48, "+ 新增试验类型", Action::AddTemplate, 0, GREEN);
  constexpr int kItemsPerPage = 6;
  const int start = list_page * kItemsPerPage;
  for (int slot = 0; slot < kItemsPerPage && start + slot < templates.count; ++slot) {
    const int i = start + slot;
    const int x = 18 + (slot / 3) * 391;
    const int y = 132 + (slot % 3) * 83;
    const LabTemplate& item = templates.items[i];
    box(x, y, 373, 79, CARD);
    label_fitted(x + 10, y + 6, 278, item.name);
    char detail[16]; snprintf(detail, sizeof detail, "%d 步", item.count);
    label(x + 298, y + 7, detail, MUTED);
    if (picker) {
      button(x + 10, y + 38, 137, 36, "编辑", Action::EditTemplate, i, BLUE);
      button(x + 155, y + 38, 208, 36, "使用", Action::UseTemplate, i, GREEN);
    } else {
      button(x + 10, y + 38, 353, 36, "编辑", Action::EditTemplate, i, BLUE);
    }
  }
  page_control(list_page,max(1,(int(templates.count)+5)/6),18,384,764,35);
  nav();
}

void draw_project_picker() { draw_templates(true); }

void draw_template_editor() {
  if (selected_template < 0 || selected_template >= templates.count) {
    page = Page::Templates; draw_templates(false); return;
  }
  LabTemplate& item = templates.items[selected_template];
  selected_step = constrain(selected_step, 0, item.count - 1);
  TemplateStep& step = item.steps[selected_step];
  title_bar("编辑试验类型");
  button(18, 75, 600, 48, item.name, Action::TemplateName, 0, BLUE);
  button(644, 75, 138, 48, "删除类型", Action::DeleteTemplate, 0, RED);
  for (int i = 0; i < item.count; ++i) {
    const int y = 133 + i * 47;
    box(18, y, 291, 44, i == selected_step ? CYAN : BLUE);
    char line[58]; snprintf(line, sizeof line, "%d. %s", i + 1, item.steps[i].name);
    label(27, y + 9, line);
    hit(18, y, 291, 44, Action::SelectStep, i);
  }
  box(322, 133, 460, 279, CARD);
  button(338, 143, 428, 48, step.name, Action::TemplateStepName, selected_step, BLUE);
  char detail[70]; snprintf(detail, sizeof detail, "等待 %d 分钟 · %s", step.minutes, mode_label(step.mode));
  button(338, 197, 428, 42, detail, Action::TemplateMinutes, 0, BLUE);
  button(338, 246, 428, 58, "切换提醒方式", Action::StepMode, selected_step);
  if (item.count < kMaxSteps)
    button(338, 325, 208, 70, "+ 增加步骤", Action::TemplateAddStep, 0, GREEN);
  if (item.count > 1)
    button(555, 325, 211, 70, "删除这步", Action::TemplateDeleteStep, 0, RED);
  nav();
}

void draw_keyboard(bool custom) {
  const bool editing = page == Page::EditText;
  if (editing && (edit_target == EditTarget::Minutes || edit_target == EditTarget::RunMinutes ||
                  edit_target == EditTarget::ProjectMinutes)) {
    title_bar("设置等待分钟");
    box(18, 73, 765, 48, CARD);
    label(28, 83, entry[0] ? entry : "输入 0—1440 分钟", entry[0] ? WHITE_ : MUTED);
    static const char digits[] = "1234567890";
    for (int i = 0; i < 10; ++i) {
      char key[2] = {digits[i], 0};
      const int col = i == 9 ? 1 : i % 3, row = i == 9 ? 3 : i / 3;
      button(86 + col * 180, 132 + row * 68, 168, 60, key, Action::Key, digits[i], KEY_BG);
    }
    button(635, 268, 145, 60, "删除字", Action::Backspace);
    button(635, 337, 145, 65, "保存", Action::SaveText, 0, GREEN);
    nav();
    return;
  }
  const char* heading = editing ? (edit_target==EditTarget::ResultGroupName?"分组名称":edit_target==EditTarget::ResultStandard?"采用规范":edit_target==EditTarget::ResultNotes?"试验备注":edit_target==EditTarget::ResultUnit?"自定义单位":edit_target == EditTarget::TemplateName ? "试验类型名称" : "步骤名称") :
                        custom ? "自定义实验项目" : "建立样品";
  if (pinyin_mode) {
    title_bar(heading);
    button(644, 9, 138, 46, "返回", Action::Back);
    box(18, 70, 764, 46, CARD);
    label(28, 80, entry[0] ? entry : "点拼音字母，再点候选字", entry[0] ? WHITE_ : MUTED);
    box(18, 123, 560, 41, CARD);
    char composing[40]; snprintf(composing, sizeof composing, "拼音: %s", pinyin_input);
    label(28, 130, composing, CYAN);
    if (custom) {
      char mins[40]; snprintf(mins, sizeof mins, "%d 分钟", project_minutes);
      button(588, 123, 194, 41, mins, Action::ProjectMinutes, 0, BLUE);
    }
    const int total = pinyin_total_choices();
    for (int i = 0; i < 5 && pinyin_page * 5 + i < total; ++i) {
      char choice[32] = {};
      if (pinyin_choice_at(pinyin_page * 5 + i, choice))
        button(18 + i * 111, 171, 105, 49, choice, Action::PinyinCandidate,
               pinyin_page * 5 + i, BLUE);
    }
    if (pinyin_page > 0) button(585, 171, 90, 49, "上页", Action::PinyinPrev);
    if ((pinyin_page + 1) * 5 < total) button(682, 171, 100, 49, "下页", Action::PinyinNext);
    static const char* rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    for (int row = 0; row < 3; ++row) {
      const int start_x = row == 0 ? 18 : row == 1 ? 56 : 132;
      const int len = strlen(rows[row]);
      for (int col = 0; col < len; ++col) {
        char key[2] = {rows[row][col], 0};
        button(start_x + col * 76, 227 + row * 58, 70, 54, key,
               Action::Key, rows[row][col], KEY_BG);
      }
    }
    button(18, 408, 155, 57, "英文/数字", Action::PinyinToggle);
    button(181, 408, 130, 57, "删除字", Action::Backspace);
    button(319, 408, 130, 57, "空格", Action::Key, ' ');
    button(457, 408, 130, 57, "清拼音", Action::PinyinClear);
    button(635, 408, 148, 57, "保存", editing ? Action::SaveText :
           custom ? Action::SaveProject : Action::SaveSample, 0, GREEN);
    return;
  }
  title_bar(heading);
  button(640, 9, 142, 46, "拼音输入", Action::PinyinToggle, 0, BLUE);
  box(18, 73, 765, 48, CARD);
  label(28, 83, entry[0] ? entry : "输入名称或编号", entry[0] ? WHITE_ : MUTED);
  static const char keys[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.";
  for (int i = 0; i < 38; ++i) {
    char key[2] = {keys[i], 0};
    const int x = 18 + (i % 10) * 76, y = 132 + (i / 10) * 57;
    button(x, y, 70, 51, key, Action::Key, keys[i], KEY_BG);
  }
  if (editing) {
    button(18, 365, 170, 44, "常用中文词", Action::CommonPhrase);
  } else if (custom) {
    char mins[32]; snprintf(mins, sizeof mins, "等待 %d 分钟", project_minutes);
    button(18, 365, 540, 44, mins, Action::ProjectMinutes, 0, BLUE);
  } else {
    button(18, 365, 170, 44, "空格", Action::Key, ' ');
  }
  button(577, 365, 95, 44, "删除字", Action::Backspace);
  button(679, 365, 104, 44, "保存", editing ? Action::SaveText :
         custom ? Action::SaveProject : Action::SaveSample, 0, GREEN);
  nav();
}

void draw_clock() {
  title_bar("设置时钟", clock_ok ? "RTC 已连接" : "RTC 未设置");
  button(490,9,154,46,"秒表",Action::StopwatchOpen,0,BLUE);
  const char* fields[] = {"年", "月", "日", "时", "分"};
  int values[] = {clock_edit.year, clock_edit.month, clock_edit.day, clock_edit.hour, clock_edit.min};
  for (int i = 0; i < 5; ++i) {
    const int x = 20 + i * 156;
    button(x, 105, 145, 48, fields[i], Action::ClockField, i, clock_field == i ? CYAN : BLUE);
    char val[12]; snprintf(val, sizeof val, "%d", values[i]);
    centered(x, 190, 145, val, WHITE_);
  }
  button(125, 277, 150, 60, "减少", Action::ClockMinus);
  button(525, 277, 150, 60, "增加", Action::ClockPlus);
  button(282, 350, 236, 59, "保存到 RTC", Action::ClockSave, 0, GREEN);
  nav();
}

void draw_settings() {
  title_bar("设置");
  button(322,9,154,46,"秒表",Action::StopwatchOpen,0,BLUE);
  button(490,9,154,46,"锁屏",Action::PocketLock,0,BLUE);
  box(20, 75, 760, 75, CARD);
  char current[48]; format_clock(current, sizeof current);
  label(38, 98, clock_ok ? current : "RTC 时间未设置", CYAN);
  button(522, 85, 240, 54, "设置时钟", Action::ClockField, -1, BLUE);
  box(20, 158, 760, 75, CARD);
  char level[40]; snprintf(level, sizeof level, "屏幕亮度  %d%%", brightness);
  label(38, 182, level);
  button(410, 168, 168, 54, "降低", Action::BrightnessMinus);
  button(594, 168, 168, 54, "提高", Action::BrightnessPlus);
  box(20, 241, 760, 75, CARD);
  char volume[40]; snprintf(volume, sizeof volume, alarm_volume ? "闹铃音量  %d%%" : "闹铃音量  %d%% 静音", alarm_volume);
  label(38, 265, volume);
  button(410, 251, 168, 54, "降低", Action::VolumeMinus);
  button(594, 251, 168, 54, "提高", Action::VolumePlus);
  button(38, 333, 724, 66, "试听闹铃旋律", Action::TestSound, 0, GREEN);
  nav();
}

void stopwatch_format(uint64_t us,char* out,size_t size) {
  const uint64_t cs=us/10000;
  snprintf(out,size,"%02llu:%02u:%02u.%02u",static_cast<unsigned long long>(cs/360000),
           unsigned(cs/6000%60),unsigned(cs/100%60),unsigned(cs%100));
}

void draw_stopwatch_digits(const char* text) {
  // Large fixed-width digits without allocating another framebuffer.
  static const uint8_t segments[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
  int width=0;
  for(const char* p=text;*p;++p)width+=(*p==':'?18:*p=='.'?12:42)+8;
  int x=(W-width+8)/2;
  constexpr int y=92;
  for(const char* p=text;*p;++p) {
    if(*p==':'){box(x+5,y+20,7,7,CYAN);box(x+5,y+48,7,7,CYAN);x+=26;}
    else if(*p=='.'){box(x+2,y+65,7,7,CYAN);x+=20;}
    else {
      const uint8_t s=segments[*p-'0'];
      if(s&1)box(x+6,y,30,6,CYAN);
      if(s&2)box(x+36,y+6,6,27,CYAN);
      if(s&4)box(x+36,y+39,6,27,CYAN);
      if(s&8)box(x+6,y+66,30,6,CYAN);
      if(s&16)box(x,y+39,6,27,CYAN);
      if(s&32)box(x,y+6,6,27,CYAN);
      if(s&64)box(x+6,y+33,30,6,CYAN);
      x+=50;
    }
  }
}

void draw_stopwatch() {
  title_bar(stopwatch.running?"秒表 · 计时中":"秒表 · 已暂停");
  button(490,9,154,46,"返回",Action::Back,0,BLUE);
  char records[32];snprintf(records,sizeof records,"记录 (%lu)",static_cast<unsigned long>(stopwatch.record_count));
  button(322,9,154,46,records,Action::StopwatchRecords,0,BLUE);
  const uint64_t cs=stopwatch.elapsed_us(esp_timer_get_time())/10000;
  char text[40];
  stopwatch_format(stopwatch.elapsed_us(esp_timer_get_time()),text,sizeof text);
  box(20,76,760,108,CARD);
  draw_stopwatch_digits(text);
  centered(20,190,760,"时 : 分 : 秒 . 百分秒",MUTED);
  if(stopwatch.record_count){
    char interval[40],latest[96];stopwatch_format(stopwatch.interval_us(stopwatch.record_count-1),interval,sizeof interval);
    snprintf(latest,sizeof latest,"第 %lu 次记录 · 本段 %s",static_cast<unsigned long>(stopwatch.record_count),interval);
    centered(20,226,760,latest,CYAN);
  }else centered(20,226,760,"按下记录，保存累计时间与分段用时",MUTED);
  if(stopwatch.running)button(20,380,200,38,"暂停",Action::StopwatchToggle,0,ORANGE);
  else centered(20,387,200,cs?"已暂停":"尚未开始",MUTED);
  char caption[64];snprintf(caption,sizeof caption,stopwatch.running?"记录（按下即记录）":cs?"继续计时":"开始计时");
  if(record_flash_until){
    if(record_flash_failed)snprintf(caption,sizeof caption,"保存失败，请检查提示");
    else if(record_flash_started)snprintf(caption,sizeof caption,record_flash_resumed?"已继续，再按记录":"已开始，再按记录");
    else snprintf(caption,sizeof caption,"已记录 第 %lu 条",static_cast<unsigned long>(stopwatch.record_count));
  }
  button(kRecordX,kRecordY,kRecordW,kRecordH,caption,Action::StopwatchPrimary,0,record_flash_until?(record_flash_failed?RED:GREEN):stopwatch.running?BLUE:GREEN);
  if(stopwatch.running){
    centered(580,387,200,"暂停后可清零",MUTED);
  }else button(580,380,200,38,"清零",Action::StopwatchReset,0,BLUE);
  centered(230,387,340,stopwatch_store.saved(stopwatch.record_count)?"记录已保存到TF卡":"无卡最多暂存100条",MUTED);
  nav();
}

void draw_stopwatch_records() {
  title_bar("秒表记录");
  button(490,9,154,46,"返回",Action::Back,0,BLUE);
  const int pages=max(1,static_cast<int>((stopwatch.record_count+3)/4));list_page=constrain(list_page,0,pages-1);
  page_control(list_page,pages,455,76,327,44);
  label(28,86,"序号 / 累计时间 / 本段用时",MUTED);
  for(int row=0;row<4;++row){
    const int index=list_page*4+row;if(index>=stopwatch.record_count)break;
    const int y=132+row*68;box(18,y,764,60,CARD);
    char number[16],total[40],interval[40];snprintf(number,sizeof number,"%02d",index+1);
    uint64_t t=0,d=0;
    if(stopwatch_store.read(stopwatch,index,t,d)){stopwatch_format(t,total,sizeof total);stopwatch_format(d,interval,sizeof interval);}
    else {snprintf(total,sizeof total,"TF记录不可读");snprintf(interval,sizeof interval,"请检查TF卡");}
    label(30,y+18,number);label(130,y+18,total,CYAN);label(455,y+18,interval);
  }
  if(!stopwatch.record_count)label(28,170,"暂无记录，返回秒表开始计时后点击记录",MUTED);
  label(28,398,"本段为相邻记录时间差；首段从开始算起",MUTED);
  nav();
}

void draw_confirm() {
  if (confirm_kind == ConfirmKind::None) return;
  hit_count = 0;
  box(112, 96, 576, 291, BG);
  const char* message = confirm_kind == ConfirmKind::Skip ? "跳过前面未完成的步骤？" :
                        confirm_kind == ConfirmKind::Early ? "尚未到点，仍确认完成？" :
                        confirm_kind == ConfirmKind::DeleteSample ? "删除样品及全部项目？" :
                        confirm_kind == ConfirmKind::DeleteRun ? "删除这个实验项目？" :
                        confirm_kind == ConfirmKind::DeleteTemplate ? "删除试验类型？已有项目保留。" :
                        confirm_kind == ConfirmKind::DeleteStep ? "删除这个模板步骤？" :
                        confirm_kind == ConfirmKind::RestoreHistory ? "恢复此项目到板内？" :
                        confirm_kind == ConfirmKind::ResetStopwatch ? "清零秒表？TF卡记录文件保留" : "";
  centered(130, 152, 540, message);
  button(166, 277, 192, 68, "取消", Action::Cancel);
  button(442, 277, 192, 68, "确定", Action::Confirm, 0, RED);
}

void draw_alarm() {
  if (alert_run < 0) return;
  hit_count = 0;
  box(72, 64, 656, 350, RED);
  centered(96, 94, 608, "实验步骤已到点");
  const Run& run = lab.runs[alert_run];
  centered(96, 161, 608, run.sample);
  char alarm_name[96]; snprintf(alarm_name,sizeof alarm_name,"%s · %s",phase_label(run.phase),run.name);
  centered(96, 204, 608, alarm_name);
  centered(96, 246, 608, run.steps[alert_step].name);
  button(121, 326, 220, 60, "停止提醒", Action::StopAlarm, 0, CARD);
  button(459, 326, 220, 60, "完成本步", Action::FinishAlarm, 0, GREEN);
}

void draw_lock_overlay() {
  if (!screen_locked) return;
  hit_count=0;
  box(0,0,W,H,BG);
  title_bar("屏幕已锁定");
  if (alert_run>=0) centered(80,110,640,"实验步骤已到点",ORANGE);
  centered(70,180,660,"从屏幕左侧向右滑动解锁",WHITE_);
  centered(70,224,660,"滑到右侧后松手",MUTED);
  box(72,300,656,58,CARD);
  box(90,312,56,34,CYAN);
  label(660,316,">>",CYAN);
}
#include "src/result_ui.hpp"

void draw_all() {
  if (!pixels || backlight_off) return;
  hit_count = 0;
  box(0, 0, W, H, BG);
  switch (page) {
    case Page::Dashboard: draw_dashboard(); break;
    case Page::SampleDates: draw_sample_dates(); break;
    case Page::HistoryList: draw_history_list(); break;
    case Page::HistoryDetail: draw_history_detail(); break;
    case Page::HistoryDate: draw_history_date(); break;
    case Page::HistoryField: draw_history_field(); break;
    case Page::Samples: draw_samples(); break;
    case Page::Sample: draw_sample(); break;
    case Page::Run: draw_run(); break;
    case Page::Results: draw_results(); break;
    case Page::ResultOptions: draw_result_options(); break;
    case Page::ResultStandards: draw_result_standards(); break;
    case Page::NumericPad: draw_numeric_pad(); break;
    case Page::ResultSummary: draw_result_summary(); break;
    case Page::ExportSelect: draw_export_select(); break;
    case Page::ExportDates: draw_export_dates(); break;
    case Page::ExportSamples: draw_export_samples(); break;
    case Page::Transfer: draw_transfer(); break;
    case Page::PageJump: draw_page_jump(); break;
    case Page::SampleRecords: draw_sample_records(); break;
    case Page::StepRecord: draw_step_record(); break;
    case Page::NewSample: draw_keyboard(false); break;
    case Page::PickProject: draw_project_picker(); break;
    case Page::NewProject: draw_keyboard(true); break;
    case Page::Templates: draw_templates(false); break;
    case Page::TemplateEdit: draw_template_editor(); break;
    case Page::EditText: draw_keyboard(false); break;
    case Page::Clock: draw_clock(); break;
    case Page::Stopwatch: draw_stopwatch(); break;
    case Page::StopwatchRecords: draw_stopwatch_records(); break;
    case Page::Settings: draw_settings(); break;
  }
  if (!storage_ok) {
    label(22, 396, "存储不可用，请勿建立新记录", RED);
  }
  if (!templates_ok) label(22, 367, "试验类型读取失败", RED);
  if (toast[0] && static_cast<int32_t>(toast_until_ms - millis()) > 0) {
    box(15, 392, 766, 29, CARD);
    label(23, 394, toast, ORANGE);
  }
  draw_confirm();
  draw_alarm();
  draw_lock_overlay();
  waveshare_rgb_lcd_display(reinterpret_cast<uint8_t*>(pixels));
  if (frame_a && frame_b) pixels = pixels == frame_a ? frame_b : frame_a;
  last_draw_ms = millis();
  redraw = false;
}

void go_back() {
  if(page==Page::StopwatchRecords){page=Page::Stopwatch;return;}
  if(page==Page::Stopwatch){page=stopwatch_return;return;}
  if(page==Page::PageJump){page=jump_parent;return;}
  list_page = step_page = 0;
  if(page==Page::PageJump)page=jump_parent;
  else if(page==Page::NumericPad)page=numeric_field>=7?Page::ResultOptions:Page::Results;
  else if(page==Page::ResultStandards)page=Page::ResultOptions;
  else if(page==Page::ResultOptions)page=Page::Results;
  else if(page==Page::Results||page==Page::ResultSummary)page=Page::Sample;
  else if(page==Page::ExportSelect)page=Page::ExportSamples;
  else if(page==Page::ExportSamples)page=Page::ExportDates;
  else if(page==Page::ExportDates)page=Page::Dashboard;
  else if(page==Page::Transfer)page=export_return;
  else if(page==Page::HistoryField)page=Page::HistoryDetail;
  else if(page==Page::HistoryDetail||page==Page::HistoryDate)page=Page::HistoryList;
  else if(page==Page::HistoryList)page=Page::SampleDates;
  else if(page==Page::EditText&&(edit_target==EditTarget::ResultGroupName||edit_target==EditTarget::ResultStandard||edit_target==EditTarget::ResultNotes||edit_target==EditTarget::ResultUnit))page=edit_target==EditTarget::ResultGroupName?Page::Results:Page::ResultOptions;
  else if (page == Page::Samples) page = Page::SampleDates;
  else if (page == Page::StepRecord) page=Page::SampleRecords;
  else if (page == Page::SampleRecords || page == Page::Run) page = Page::Sample;
  else if (page == Page::Sample || page == Page::NewSample) page = Page::Samples;
  else if (page == Page::PickProject) page = Page::Sample;
  else if (page == Page::NewProject) page = Page::PickProject;
  else if (page == Page::EditText)
    page = edit_target == EditTarget::RunMinutes ? Page::Run :
           edit_target == EditTarget::ProjectMinutes ? Page::NewProject : Page::TemplateEdit;
  else if (page == Page::TemplateEdit)
    page = templates_from_picker ? Page::PickProject : Page::Templates;
  else if (page == Page::Templates) page = Page::Dashboard;
  else if (page == Page::Clock) page = Page::Settings;
  else page = Page::Dashboard;
}

void make_generic(Run& run, const char* name, int minutes) {
  memset(&run, 0, sizeof run);
  strncpy(run.name, name, sizeof(run.name) - 1);
  run.count = minutes > 0 ? 1 : 2;
  if (minutes > 0) step_clear(&run.steps[0], "等待", minutes, RemindMode::Alarm);
  else {
    step_clear(&run.steps[0], "样品准备", 0, RemindMode::None);
    step_clear(&run.steps[1], "测定与录入读数", 0, RemindMode::None);
  }
}

void add_from_template(int template_index) {
  if (selected_sample < 0 || selected_sample >= lab.sample_count ||
      template_index < 0 || template_index >= templates.count) return;
  if(!clock_ok){notice("请先设置时钟，再创建项目");return;}
  if(lab.run_count>=kMaxRuns) {
    const uint32_t sample_id=lab.samples[selected_sample].id;
    if(!history_make_room(false,sample_id)){notice("项目已满，暂无可自动归档的项目");return;}
    selected_sample=lab_find_sample(sample_id);if(selected_sample<0){notice("样品不存在");return;}
  }
  const LabTemplate& source = templates.items[template_index];
  Run run{};
  run.phase = project_phase;
  strncpy(run.name, source.name, sizeof(run.name) - 1);
  run.count = source.count;
  for (int i = 0; i < run.count; ++i)
    step_clear(&run.steps[i], source.steps[i].name, source.steps[i].minutes, source.steps[i].mode);
  const int index = lab_add_run(selected_sample, run, clock_ok ? now_ms() : 0);
  if (index < 0) { notice("创建失败，请检查存储或项目数量"); return; }
  selected_run = index; selected_step = 0; list_page = 0; page = Page::Sample;
  notice("已添加实验项目");
}

void start_or_finish(int step_index, bool confirmed) {
  if (!clock_ok) { notice("请先设置 RTC 时钟"); return; }
  if (selected_run < 0 || selected_run >= lab.run_count) return;
  Run& run = lab.runs[selected_run];
  if (step_index < 0 || step_index >= run.count) return;
  Step& step = run.steps[step_index];
  if (!step.started) {
    const StartResult result = start_step(run, step_index, now_ms(), confirmed);
    if (result == StartResult::NeedSkip) {
      pending_run = selected_run; pending_step = step_index;
      confirm_kind = ConfirmKind::Skip;
      return;
    }
    if (result == StartResult::Ok) {
      if (!lab_save()) notice("计时保存失败");
      else notice("已开始计时");
    }
  } else {
    const FinishResult result = finish_step(run, step_index, now_ms(), confirmed);
    if (result == FinishResult::NeedEarly) {
      pending_run = selected_run; pending_step = step_index;
      confirm_kind = ConfirmKind::Early;
      return;
    }
    if (result == FinishResult::Ok) {
      if (!lab_save()) notice("完成状态保存失败");
      else notice("已确认完成，可开始下一步");
      if (alert_run == selected_run && alert_step == step_index) alert_run = alert_step = -1;
    }
  }
}

void handle_action(Action action, int arg) {
  if(handle_result_action(action,arg))return;
  switch (action) {
    case Action::JumpOpen:
      jump_parent=page;jump_total=max(1,arg);jump_entry[0]=0;page=Page::PageJump;break;
    case Action::JumpKey:
      if(arg=='C')jump_entry[0]=0;
      else if(arg=='<'){size_t n=strlen(jump_entry);if(n)jump_entry[n-1]=0;}
      else if(arg>='0'&&arg<='9'){size_t n=strlen(jump_entry);if(n<sizeof jump_entry-1){jump_entry[n]=char(arg);jump_entry[n+1]=0;}}
      break;
    case Action::JumpGo: {
      int value=atoi(jump_entry);if(value<1||value>jump_total){notice("请输入有效页码");break;}
      if(jump_parent==Page::ResultSummary)summary_page=value-1;else list_page=value-1;page=jump_parent;break;
    }
    case Action::PocketLock: lock_display(); break;
    case Action::SampleRecords: page=Page::SampleRecords;list_page=0;break;
    case Action::HistoryOpen: history_date_filter=0;list_page=0;page=Page::HistoryList;break;
    case Action::HistoryAll: history_date_filter=0;list_page=0;page=Page::HistoryList;break;
    case Action::HistoryDateOpen: memset(history_date_entry,0,sizeof history_date_entry);page=Page::HistoryDate;break;
    case Action::HistoryDateKey: {
      size_t n=strlen(history_date_entry);
      if(arg=='C')history_date_entry[0]=0;else if(arg=='<'){if(n)history_date_entry[n-1]=0;}
      else if(arg>='0'&&arg<='9'&&n<8){history_date_entry[n]=char(arg);history_date_entry[n+1]=0;}break;
    }
    case Action::HistoryDateApply: {
      if(strlen(history_date_entry)!=8){notice("请输入八位日期");break;}
      const int key=atoi(history_date_entry),year=key/10000,month=(key/100)%100,day=key%100;
      struct tm date{};date.tm_year=year-1900;date.tm_mon=month-1;date.tm_mday=day;date.tm_hour=12;
      if(year<2020||year>2099||month<1||month>12||day<1||day>31||mktime(&date)==time_t(-1)||
         date.tm_year!=year-1900||date.tm_mon!=month-1||date.tm_mday!=day){notice("日期无效");break;}
      history_date_filter=key;list_page=0;page=Page::HistoryList;break;
    }
    case Action::HistoryItemOpen: {
      if(arg<0||arg>=4||!history_rows[arg].id)break;
      const HistoryRow& row=history_rows[arg];history_selected_id=row.id;history_selected_archived=row.archived;
      history_has_comparison=false;history_comparison_baseline=0;memset(history_comparisons,0,sizeof history_comparisons);
      if(row.archived) {
        if(!history_read(row.id,history_sample,history_run,history_result,history_has_result,nullptr,history_comparisons,&history_comparison_baseline,&history_has_comparison)){notice("归档无法读取");break;}
      }else {
        int index=lab_find_run(row.id);if(index<0){notice("项目不存在");break;}
        history_run=lab.runs[index];int sample=lab_find_sample(history_run.sample_id);if(sample<0){notice("样品不存在");break;}
        history_sample=lab.samples[sample];const ResultRecord* result=results_for(history_run);
        history_has_result=result!=nullptr;if(result){history_result=*result;
          if(history_run.phase!=AgingPhase::Original&&result->metric!=Metric::Mass&&result->metric!=Metric::Other){history_has_comparison=true;
            for(int g=0;g<result->group_count;++g)history_comparisons[g]=result_derived(history_run,g,&history_comparison_baseline);}}
      }
      list_page=0;page=Page::HistoryDetail;break;
    }
    case Action::HistoryExportRow: {
      const int previous_page=list_page;
      handle_action(Action::HistoryItemOpen,arg);
      if(page==Page::HistoryDetail) {
        handle_action(Action::HistoryExport,0);
        if(page==Page::Transfer)export_return=Page::HistoryList;
        else page=Page::HistoryList;
      }
      list_page=previous_page;break;
    }
    case Action::HistoryFieldOpen:
      if(arg>=0&&arg<history_run.count+(history_has_result?history_result.group_count:0)){history_field=arg;page=Page::HistoryField;}break;
    case Action::HistoryRestore:
      if(history_selected_archived)confirm_kind=ConfirmKind::RestoreHistory;
      else {int index=lab_find_run(history_selected_id);if(index<0){notice("项目不存在");break;}
        selected_run=index;selected_sample=lab_find_sample(lab.runs[index].sample_id);selected_step=0;list_page=0;page=Page::Run;}
      break;
    case Action::HistoryExport: {
      if(voice_playing||voice_upload_active){notice("请先结束当前语音播放或传输");break;}
      if(history_selected_archived&&transfer_active()&&transfer_report_only()&&history_shared_id==history_selected_id) {
        export_return=Page::HistoryDetail;qr_wifi=true;prepare_transfer_qr();page=Page::Transfer;break;
      }
      bool ok=false;
      if(history_selected_archived)ok=history_report(history_selected_id,exported_path);
      else {int index=lab_find_run(history_selected_id);if(index>=0)ok=result_export(uint32_t(1)<<index,true,exported_path);}
      if(!ok){notice("导出失败，请检查TF卡");break;}
      export_return=Page::HistoryDetail;
      if(!transfer_start_report(exported_path)){notice("报告已保存，热点开启失败");break;}
      history_shared_id=history_selected_id;
      qr_wifi=true;prepare_transfer_qr();page=Page::Transfer;break;
    }
    case Action::OpenRecord: selected_run=arg;selected_step=0;page=Page::StepRecord;break;
    case Action::Home: page = Page::Dashboard; list_page = 0; task_tab = 0; break;
    case Action::Samples: page = Page::SampleDates; list_page = 0; break;
    case Action::OpenDate: selected_date=arg; page=Page::Samples; list_page=0; break;
    case Action::OpenTemplates:
      page = Page::Templates; list_page = 0; templates_from_picker = false; break;
    case Action::OpenSettings: page = Page::Settings; break;
    case Action::Back: go_back(); break;
    case Action::Next: ++list_page; break;
    case Action::Prev: list_page = max(0, list_page - 1); break;
    case Action::TaskTab: task_tab = arg; list_page = 0; break;
    case Action::OpenSample: selected_sample = arg; page = Page::Sample; list_page = 0; break;
    case Action::OpenRun:
      selected_run = arg; selected_sample = lab_find_sample(lab.runs[arg].sample_id);
      if (selected_sample>=0) selected_date=sample_date_key(lab.samples[selected_sample]);
      selected_step = current_index(lab.runs[arg]);
      if (selected_step < 0) selected_step = next_pending_index(lab.runs[arg]);
      if (selected_step < 0) selected_step = 0;
      page = Page::Run; list_page = 0; break;
    case Action::NewSample: memset(entry, 0, sizeof entry); page = Page::NewSample; start_pinyin_name(); break;
    case Action::SaveSample: {
      if (pinyin_mode && pinyin_input[0]) { notice("请先选拼音候选字"); break; }
      if(lab.sample_count>=kMaxSamples&&!history_make_room(true,0)){notice("样品已满，暂无可自动归档的项目");break;}
      const int i = lab_add_sample(entry, now_ms());
      if (i < 0) notice("请输入编号，或检查存储空间");
      else { selected_sample = i; selected_date=sample_date_key(lab.samples[i]); page = Page::Sample; list_page = 0; }
      break;
    }
    case Action::PickProject: page = Page::PickProject; list_page = 0; templates_from_picker = true; project_phase = AgingPhase::Original; break;
    case Action::AddPreset: add_from_template(arg); break;
    case Action::UseTemplate: add_from_template(arg); break;
    case Action::ProjectPhase:
      if (arg>=0 && arg<=2) { project_phase=static_cast<AgingPhase>(arg); list_page=0; }
      break;
    case Action::RunPhase:
      if (selected_run>=0 && selected_run<lab.run_count) {
        Run& run=lab.runs[selected_run];
        const AgingPhase before=run.phase;
        run.phase=static_cast<AgingPhase>((static_cast<int>(before)+1)%3);
        lab_save_pending=false;
        if (!lab_save()) { run.phase=before; notice("阶段保存失败"); }
      }
      break;
    case Action::EditTemplate:
      selected_template = arg; selected_step = 0;
      templates_from_picker = page == Page::PickProject;
      page = Page::TemplateEdit; break;
    case Action::AddTemplate:
      templates_from_picker = page == Page::PickProject;
      type_save_pending = false;
      selected_template = templates_add();
      if (selected_template < 0) notice("新增试验类型失败");
      else { selected_step = 0; page = Page::TemplateEdit; }
      break;
    case Action::SelectStep: selected_step = arg; break;
    case Action::TemplateName:
      if (selected_template < 0) break;
      edit_target = EditTarget::TemplateName; phrase_index = 0;
      strncpy(entry, templates.items[selected_template].name, sizeof(entry) - 1);
      entry[sizeof(entry) - 1] = 0; page = Page::EditText; start_pinyin_name(); break;
    case Action::TemplateStepName:
      if (selected_template < 0) break;
      edit_target = EditTarget::StepName; phrase_index = 0;
      strncpy(entry, templates.items[selected_template].steps[selected_step].name, sizeof(entry) - 1);
      entry[sizeof(entry) - 1] = 0; page = Page::EditText; start_pinyin_name(); break;
    case Action::TemplateMinutes:
      if (selected_template < 0) break;
      edit_target = EditTarget::Minutes;
      snprintf(entry, sizeof(entry), "%d", templates.items[selected_template].steps[selected_step].minutes);
      page = Page::EditText; break;
    case Action::RunMinutes:
      if (selected_run < 0 || selected_run >= lab.run_count) break;
      edit_target = EditTarget::RunMinutes;
      selected_step = arg;
      snprintf(entry, sizeof(entry), "%d", lab.runs[selected_run].steps[selected_step].minutes);
      page = Page::EditText; break;
    case Action::ProjectMinutes:
      edit_target = EditTarget::ProjectMinutes;
      snprintf(entry, sizeof(entry), "%d", project_minutes);
      page = Page::EditText; break;
    case Action::TemplateAddStep:
      if (selected_template >= 0) {
        LabTemplate& item = templates.items[selected_template];
        if (item.count < kMaxSteps) {
          selected_step = item.count++;
          TemplateStep& step = item.steps[selected_step];
          memset(&step, 0, sizeof step);
          strncpy(step.name, "新步骤", sizeof(step.name) - 1);
          step.mode = RemindMode::None;
          if (!save_type_now()) notice("保存步骤失败");
        }
      }
      break;
    case Action::TemplateDeleteStep: confirm_kind = ConfirmKind::DeleteStep; break;
    case Action::DeleteTemplate: confirm_kind = ConfirmKind::DeleteTemplate; break;
    case Action::NewProject: memset(entry, 0, sizeof entry); project_minutes = 30; page = Page::NewProject; start_pinyin_name(); break;
    case Action::SaveProject: {
      if (pinyin_mode && pinyin_input[0]) { notice("请先选拼音候选字"); break; }
      if (!entry[0]) { notice("请输入实验项目名称"); break; }
      if(!clock_ok){notice("请先设置时钟，再创建项目");break;}
      if(selected_sample<0||selected_sample>=lab.sample_count){notice("样品不存在");break;}
      if(lab.run_count>=kMaxRuns) {
        const uint32_t sample_id=lab.samples[selected_sample].id;
        if(!history_make_room(false,sample_id)){notice("项目已满，暂无可自动归档的项目");break;}
        selected_sample=lab_find_sample(sample_id);if(selected_sample<0){notice("样品不存在");break;}
      }
      Run run{}; make_generic(run, entry, project_minutes); run.phase=project_phase;
      const int i = lab_add_run(selected_sample, run, clock_ok ? now_ms() : 0);
      if (i < 0) notice("创建失败，请检查存储或项目数量");
      else { selected_run = i; page = Page::Sample; step_page = 0; list_page=0; }
      break;
    }
    case Action::Key: {
      if (name_keyboard_active() && pinyin_mode && arg >= 'A' && arg <= 'Z') {
        const size_t len = strlen(pinyin_input);
        if (len + 1 < sizeof pinyin_input) {
          pinyin_input[len] = static_cast<char>(arg + ('a' - 'A'));
          pinyin_input[len + 1] = 0;
          pinyin_page = 0;
        }
        break;
      }
      if (name_keyboard_active() && pinyin_mode && arg == ' ' && pinyin_input[0]) {
        notice("请先选拼音候选字");
        break;
      }
      const size_t len = strlen(entry);
      if (len + 1 <= entry_capacity()) { entry[len] = static_cast<char>(arg); entry[len + 1] = 0; }
      break;
    }
    case Action::Backspace: {
      if (name_keyboard_active() && pinyin_mode && pinyin_input[0]) {
        pinyin_input[strlen(pinyin_input) - 1] = 0;
        pinyin_page = 0;
        break;
      }
      size_t len = strlen(entry);
      if (len) {
        --len;
        while (len > 0 && (static_cast<unsigned char>(entry[len]) & 0xC0) == 0x80) --len;
        entry[len] = 0;
      }
      break;
    }
    case Action::PinyinToggle: pinyin_mode = !pinyin_mode; break;
    case Action::PinyinClear: pinyin_input[0] = 0; pinyin_page = 0; break;
    case Action::PinyinNext: ++pinyin_page; break;
    case Action::PinyinPrev: pinyin_page = max(0, pinyin_page - 1); break;
    case Action::PinyinCandidate: {
      char character[32] = {};
      if (!pinyin_choice_at(arg, character)) break;
      const size_t len = strlen(entry), bytes = strlen(character);
      if (len + bytes > entry_capacity()) { notice("名称已达到长度上限"); break; }
      memcpy(entry + len, character, bytes + 1);
      pinyin_input[0] = 0;
      pinyin_page = 0;
      break;
    }
    case Action::CommonPhrase: {
      static const char* type_names[] = {"针入度", "软化点", "延度", "闪点", "样品加热", "RTFOT 老化", "质量变化"};
      static const char* step_names[] = {"样品准备", "室温冷却", "水浴", "加热", "保温", "搅拌", "冷却", "取样", "测定与录入读数"};
      const bool type = edit_target == EditTarget::TemplateName;
      const char* phrase = type ? type_names[phrase_index % 7] : step_names[phrase_index % 9];
      ++phrase_index;
      strncpy(entry, phrase, sizeof(entry) - 1); entry[sizeof(entry) - 1] = 0;
      break;
    }
    case Action::SaveText: {
      if(edit_target==EditTarget::ResultGroupName||edit_target==EditTarget::ResultStandard||edit_target==EditTarget::ResultNotes||edit_target==EditTarget::ResultUnit){save_result_text();break;}
      if ((edit_target == EditTarget::TemplateName || edit_target == EditTarget::StepName) &&
          pinyin_mode && pinyin_input[0]) { notice("请先选拼音候选字"); break; }
      if (edit_target == EditTarget::Minutes || edit_target == EditTarget::RunMinutes ||
          edit_target == EditTarget::ProjectMinutes) {
        if (!entry[0]) { notice("请输入等待分钟数"); break; }
        for (const char* p = entry; *p; ++p)
          if (*p < '0' || *p > '9') { notice("分钟数只能输入数字"); return; }
        const long minutes = atol(entry);
        if (minutes < 0 || minutes > 1440) { notice("请输入 0—1440 分钟"); break; }
        if (edit_target == EditTarget::ProjectMinutes) {
          project_minutes = minutes;
          page = Page::NewProject;
          break;
        }
        if (edit_target == EditTarget::RunMinutes) {
          if (selected_run < 0 || selected_run >= lab.run_count) break;
          Step& step = lab.runs[selected_run].steps[selected_step];
          const RemindMode mode = minutes == 0 ? RemindMode::None :
              (step.minutes == 0 && step.mode == RemindMode::None ? RemindMode::Alarm : step.mode);
          if (set_wait(step, minutes, mode) != WaitResult::Ok) { notice("时间设置失败"); break; }
          if (step.started) {
            lab_save_pending = false;
            if (!lab_save()) notice("项目时间保存失败");
          } else schedule_lab_save();
          page = Page::Run;
          break;
        }
        if (selected_template < 0 || selected_template >= templates.count) break;
        TemplateStep& step = templates.items[selected_template].steps[selected_step];
        if (minutes == 0) step.mode = RemindMode::None;
        else if (step.minutes == 0 && step.mode == RemindMode::None) step.mode = RemindMode::Alarm;
        step.minutes = minutes;
      } else {
        if (selected_template < 0 || selected_template >= templates.count) break;
        LabTemplate& item = templates.items[selected_template];
        if (!entry[0]) { notice("名称不能为空"); break; }
        char* destination = edit_target == EditTarget::TemplateName ? item.name : item.steps[selected_step].name;
        strncpy(destination, entry, 31); destination[31] = 0;
      }
      if (!save_type_now()) notice("试验类型保存失败");
      page = Page::TemplateEdit;
      break;
    }
    case Action::StepMinus:
      if (arg < 0) project_minutes = max(0, project_minutes - 1);
      else if (page == Page::TemplateEdit && selected_template >= 0) {
        TemplateStep& step = templates.items[selected_template].steps[arg];
        step.minutes = max(0, static_cast<int>(step.minutes) - 1);
        if (!step.minutes) step.mode = RemindMode::None;
        schedule_type_save();
      }
      else if (selected_run >= 0 && arg < lab.runs[selected_run].count) {
        Step& s = lab.runs[selected_run].steps[arg];
        set_wait(s, max(0, s.minutes - 1), s.minutes <= 1 ? RemindMode::None : s.mode);
        schedule_lab_save();
      }
      break;
    case Action::StepPlus:
      if (arg < 0) project_minutes = min(1440, project_minutes + 1);
      else if (page == Page::TemplateEdit && selected_template >= 0) {
        TemplateStep& step = templates.items[selected_template].steps[arg];
        step.minutes = min(1440, static_cast<int>(step.minutes) + 1);
        schedule_type_save();
      }
      else if (selected_run >= 0 && arg < lab.runs[selected_run].count) {
        Step& s = lab.runs[selected_run].steps[arg];
        set_wait(s, min(1440, s.minutes + 1), s.mode);
        schedule_lab_save();
      }
      break;
    case Action::StepMode:
      if (page == Page::TemplateEdit && selected_template >= 0) {
        TemplateStep& step = templates.items[selected_template].steps[arg];
        RemindMode mode = step.mode == RemindMode::None ? RemindMode::Notification :
                          step.mode == RemindMode::Notification ? RemindMode::Alarm : RemindMode::None;
        if (mode != RemindMode::None && !step.minutes) notice("请先设置等待时间");
        else { step.mode = mode; if (!save_type_now()) notice("试验类型保存失败"); }
      } else if (selected_run >= 0 && arg < lab.runs[selected_run].count) {
        Step& s = lab.runs[selected_run].steps[arg];
        RemindMode mode = s.mode == RemindMode::None ? RemindMode::Notification :
                          s.mode == RemindMode::Notification ? RemindMode::Alarm : RemindMode::None;
        if (set_wait(s, s.minutes, mode) == WaitResult::NeedMinutes) notice("请先增加等待时间");
        else { lab_save_pending = false; lab_save(); }
      }
      break;
    case Action::StepAction: lab_save_pending = false; start_or_finish(arg, false); break;
    case Action::DeleteSample: confirm_kind = ConfirmKind::DeleteSample; break;
    case Action::DeleteRun: confirm_kind = ConfirmKind::DeleteRun; break;
    case Action::Cancel: confirm_kind = ConfirmKind::None; break;
    case Action::Confirm: {
      const ConfirmKind kind = confirm_kind; confirm_kind = ConfirmKind::None;
      if (kind == ConfirmKind::Skip || kind == ConfirmKind::Early) {
        selected_run = pending_run; start_or_finish(pending_step, true);
      } else if (kind == ConfirmKind::ResetStopwatch) {
        if(stopwatch.reset()){stopwatch_store.reset();record_flash_until=0;}
      } else if (kind == ConfirmKind::RestoreHistory) {
        if(alert_run>=0||voice_playing||voice_upload_active||transfer_active()){notice("请先结束提醒或传输");break;}
        const char* err=history_restore(history_selected_id,clock_ok?now_ms():0);
        if(err){notice(err);break;}
        selected_run=lab_find_run(history_selected_id);selected_sample=selected_run>=0?lab_find_sample(lab.runs[selected_run].sample_id):-1;
        selected_step=0;list_page=0;page=Page::Run;notice("已恢复，24小时内不会自动归档");
      } else if (kind == ConfirmKind::DeleteSample) {
        if (!lab_remove_sample(selected_sample)) notice("删除未保存");
        page = Page::Samples; selected_sample = -1; selected_run = -1; list_page = 0;
      } else if (kind == ConfirmKind::DeleteRun) {
        if (!lab_remove_run(selected_run)) notice("删除未保存");
        page = Page::Sample; selected_run = -1; list_page = 0;
      } else if (kind == ConfirmKind::DeleteTemplate) {
        type_save_pending = false;
        if (!templates_remove(selected_template)) notice("删除试验类型失败");
        page = templates_from_picker ? Page::PickProject : Page::Templates;
        selected_template = -1; list_page = 0;
      } else if (kind == ConfirmKind::DeleteStep && selected_template >= 0) {
        LabTemplate& item = templates.items[selected_template];
        if (item.count > 1) {
          for (int i = selected_step; i < item.count - 1; ++i) item.steps[i] = item.steps[i + 1];
          --item.count;
          memset(&item.steps[item.count], 0, sizeof(TemplateStep));
          selected_step = min(selected_step, static_cast<int>(item.count) - 1);
          if (!save_type_now()) notice("删除步骤保存失败");
        }
      }
      break;
    }
    case Action::StopAlarm: voice_stop(); alert_run = alert_step = -1; speaker_audio_poweramp_enable(false); break;
    case Action::FinishAlarm:
      voice_stop();
      selected_run = alert_run; start_or_finish(alert_step, true);
      alert_run = alert_step = -1; break;
    case Action::StopwatchOpen:
      stopwatch_return=page;page=Page::Stopwatch;break;
    case Action::StopwatchToggle:
      record_flash_until=0;
      if(stopwatch.running)stopwatch.pause(esp_timer_get_time());
      else stopwatch.start(esp_timer_get_time());
      break;
    case Action::StopwatchReset:
      if(!stopwatch.running){if(stopwatch.record_count)confirm_kind=ConfirmKind::ResetStopwatch;else {stopwatch.reset();stopwatch_store.reset();}}break;
    case Action::StopwatchPrimary:
    case Action::StopwatchRecord: break; // Dedicated down-edge path; never count release twice.
    case Action::StopwatchRecords: list_page=0;page=Page::StopwatchRecords;break;
    case Action::ClockField:
      if (arg == -1) {
        PCF85063A_Read_now(&clock_edit);
        if (!valid_time(clock_edit)) clock_edit = {2026, 1, 1, 0, 8, 0, 0};
        page = Page::Clock;
      } else clock_field = arg;
      break;
    case Action::ClockMinus:
    case Action::ClockPlus: {
      const int delta = action == Action::ClockPlus ? 1 : -1;
      if (clock_field == 0) { int n = constrain(static_cast<int>(clock_edit.year) + delta, 2024, 2069); clock_edit.year = n; }
      else {
        uint8_t* field = clock_field == 1 ? &clock_edit.month : clock_field == 2 ? &clock_edit.day :
                         clock_field == 3 ? &clock_edit.hour : &clock_edit.min;
        const int low = clock_field == 1 || clock_field == 2 ? 1 : 0;
        const int high = clock_field == 1 ? 12 : clock_field == 2 ? 31 : clock_field == 3 ? 23 : 59;
        *field = constrain(static_cast<int>(*field) + delta, low, high);
      }
      break;
    }
    case Action::ClockSave: save_clock(); page = Page::Settings; break;
    case Action::Brightness: apply_brightness(brightness >= 100 ? 30 : brightness + 20); break;
    case Action::BrightnessMinus: apply_brightness(max(10, brightness - 10)); break;
    case Action::BrightnessPlus: apply_brightness(min(100, brightness + 10)); break;
    case Action::VolumeMinus:
    case Action::VolumePlus: {
      const int next = constrain(alarm_volume + (action == Action::VolumePlus ? 10 : -10), 0, 100);
      if (next != alarm_volume) { alarm_volume = next; sound_save_pending = true; }
      break;
    }
    case Action::TestSound: if (alarm_volume) alarm_melody(); else notice("闹铃音量为零"); break;
    default: break;
  }
  redraw = true;
}

void stopwatch_record_at(uint64_t at) {
  const uint32_t began=millis();
  record_flash_started=false;
  record_flash_failed=!stopwatch_store.append(stopwatch,stopwatch.elapsed_us(at));
  if(!record_flash_failed){
    stopwatch.record(at);record_tone_remaining=2205;record_tone_phase=0;
    Serial.printf("STOPWATCH_RECORD id=%lu total_us=%llu save_ms=%lu\n",static_cast<unsigned long>(stopwatch.record_count),static_cast<unsigned long long>(stopwatch.elapsed_us(at)),static_cast<unsigned long>(millis()-began));
  }else {notice(stopwatch_store.error());Serial.printf("STOPWATCH_RECORD failed=%s\n",stopwatch_store.error());}
  record_flash_until=millis()+500;redraw=true;
}

void stopwatch_primary_at(uint64_t at) {
  if(stopwatch.running){stopwatch_record_at(at);return;}
  record_flash_resumed=stopwatch.elapsed_us(at)>0;
  stopwatch.start(at);
  record_flash_started=true;record_flash_failed=false;record_flash_until=millis()+500;
  record_tone_remaining=2205;record_tone_phase=0;redraw=true;
  Serial.printf("STOPWATCH_START resumed=%d\n",record_flash_resumed);
}

void handle_touch(int x, int y) {
  for (int i = hit_count - 1; i >= 0; --i) {
    const Hit& h = hits[i];
    if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
      handle_action(h.action, h.arg); return;
    }
  }
}

void handle_swipe(int delta_y) {
  const int direction = delta_y < 0 ? 1 : -1;
  int max_page = 0;
  if(page==Page::PageJump||page==Page::ResultStandards||page==Page::NumericPad||page==Page::Results||page==Page::ResultOptions||page==Page::Transfer||page==Page::HistoryField)return;
  if(page==Page::StopwatchRecords){max_page=max(0,(static_cast<int>(stopwatch.record_count)-1)/4);}
  else if(page==Page::HistoryDetail){max_page=max(0,(history_run.count+(history_has_result?history_result.group_count:0)-1)/4);}
  else if(page==Page::HistoryList){int count=0;const int64_t now=now_ms();for(int i=0;i<lab.run_count;++i)if(history_is_past(lab.runs[i],now)&&(!history_date_filter||history_date_key(run_age_get(lab.runs[i].id))==history_date_filter))++count;count+=max(0,history_list(0,nullptr,0,history_date_filter));max_page=max(0,(count-1)/4);}
  else if(page==Page::ResultSummary){if(direction<0)summary_page=max(0,summary_page-1);redraw=true;return;}
  else if(page==Page::ExportSelect)max_page=max(0,(export_sample_run_count()-1)/4);
  else if(page==Page::ExportDates){int keys[kMaxSamples]={};max_page=max(0,(sample_dates(keys)-1)/4);}
  else if(page==Page::ExportSamples)max_page=max(0,(samples_on_date(export_date)-1)/4);
  else if (page == Page::SampleDates) { int keys[kMaxSamples]={}; max_page=max(0,(sample_dates(keys)-1)/4); }
  else if (page == Page::Samples) max_page=max(0,(samples_on_date(selected_date)-1)/4);
  else if (page == Page::Sample && selected_sample >= 0)
    max_page = max(0, (lab_runs_for_sample(lab.samples[selected_sample].id) - 1) / 4);
  else if (page == Page::SampleRecords && selected_sample >= 0)
    max_page = max(0, (lab_runs_for_sample(lab.samples[selected_sample].id) - 1) / 4);
  else if (page == Page::Templates || page == Page::PickProject)
    max_page = max(0, (static_cast<int>(templates.count) - 1) / 6);
  else if (page == Page::Dashboard) {
    int active[kMaxRuns], due[kMaxRuns], na = 0, nd = 0;
    task_lists(active, na, due, nd);
    max_page = max(0, ((task_tab == 0 ? na : nd) - 1) / 3);
  } else if ((page == Page::Run || page == Page::StepRecord) && selected_run >= 0) {
    selected_step = constrain(selected_step + direction, 0, lab.runs[selected_run].count - 1);
    redraw = true;
    return;
  } else if (page == Page::TemplateEdit && selected_template >= 0) {
    selected_step = constrain(selected_step + direction, 0, templates.items[selected_template].count - 1);
    redraw = true;
    return;
  }
  list_page = constrain(list_page + direction, 0, max_page);
  redraw = true;
}

void check_reminders(uint32_t tick) {
  if (!clock_ok) return;
  if (alert_run >= 0) {
    if (static_cast<int32_t>(tick - alert_end_ms) >= 0) {
      voice_stop();
      alert_run = alert_step = -1;
      redraw = true;
    } else {
      if (static_cast<int32_t>(tick - next_beep_ms) >= 0 &&
          static_cast<int32_t>(alarm_sound_end_ms - tick) >= 750 &&
          lab.runs[alert_run].steps[alert_step].mode == RemindMode::Alarm) {
        if (!voice_attempted) {
          voice_attempted = true;
          if (voice_start(lab.runs[alert_run], lab.runs[alert_run].steps[alert_step])) {
            next_beep_ms = alarm_sound_end_ms;
            return;
          }
        }
        if (!voice_playing) { alarm_melody(); next_beep_ms = tick + 800; }
      }
      return;
    }
  }
  const int64_t at = now_ms();
  for (int i = 0; i < lab.run_count; ++i) {
    Run& run = lab.runs[i];
    const int j = current_index(run);
    if (j < 0) continue;
    Step& step = run.steps[j];
    if (step.notified || !is_due(step, at)) continue;
    step.notified = true;
    if (!lab_save()) notice("提醒状态保存失败");
    const uint32_t alert_tick = millis();
    Serial.printf("ALERT run=%u step=%d mode=%u save_ms=%lu\n", run.id, j, unsigned(step.mode), static_cast<unsigned long>(alert_tick - tick));
    alert_run = i; alert_step = j;
    voice_attempted = false;
    alert_end_ms = alert_tick + (step.mode == RemindMode::Alarm ? 30000 : 8000);
    alarm_sound_end_ms = alert_tick + 5000;
    next_beep_ms = alert_tick;
    apply_brightness(brightness);
    if (step.mode == RemindMode::Notification) {
      voice_attempted = true;
      if (!voice_start(run, step)) alarm_melody();
    }
    redraw = true;
    break;
  }
}

bool live_display_needed() {
  if (page == Page::Run) {
    const int j = active_step(selected_run);
    return j >= 0 && lab.runs[selected_run].steps[j].minutes > 0;
  }
  if (page == Page::Sample && selected_sample >= 0 && selected_sample < lab.sample_count) {
    const uint32_t sample_id = lab.samples[selected_sample].id;
    for (int i = 0; i < lab.run_count; ++i) {
      const int j = active_step(i);
      if (lab.runs[i].sample_id == sample_id && j >= 0 && lab.runs[i].steps[j].minutes > 0) return true;
    }
    return false;
  }
  if (page == Page::Dashboard) {
    for (int i = 0; i < lab.run_count; ++i)
      if (active_step(i) >= 0 && lab.runs[i].steps[active_step(i)].minutes > 0) return true;
  }
  return false;
}

void serial_hex_text(const char* value) {
  static const char hex[] = "0123456789abcdef";
  while (*value) {
    const uint8_t c = static_cast<uint8_t>(*value++);
    Serial.write(hex[c >> 4]);
    Serial.write(hex[c & 15]);
  }
}

void serial_voice_pair(const char* project, const char* step) {
  Serial.print("VOICE ");
  serial_hex_text(project);
  Serial.write(' ');
  serial_hex_text(step);
  Serial.write('\n');
}

void serial_voice_list() {
  Serial.println("VOICE_LIST_BEGIN 1");
  for (int i = 0; i < templates.count; ++i)
    for (int j = 0; j < templates.items[i].count; ++j)
      serial_voice_pair(templates.items[i].name, templates.items[i].steps[j].name);
  for (int i = 0; i < lab.run_count; ++i)
    for (int j = 0; j < lab.runs[i].count; ++j)
      serial_voice_pair(lab.runs[i].name, lab.runs[i].steps[j].name);
  Serial.println("VOICE_LIST_END");
}

uint32_t voice_crc_update(uint32_t crc, const uint8_t* data, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return crc;
}

void voice_upload_abort(const char* why) {
  voice_upload_file.close();
  SD_MMC.remove(voice_upload_temp);
  voice_upload_active = false;
  Serial.printf("VOICE_ERROR %s\n", why);
}

void voice_upload_begin(const char* line) {
  char name[17] = {}, extra = 0;
  unsigned long size = 0, crc = 0;
  if (sscanf(line, "voice-put %16s %lu %lx %c", name, &size, &crc, &extra) != 3 || strlen(name) != 16 ||
      size == 0 || size > 846720 || size % 4 != 0 || !voice_sd_ready || voice_playing || transfer_active()) {
    Serial.println("VOICE_ERROR bad_request"); return;
  }
  for (int i = 0; i < 16; ++i)
    if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) {
      Serial.println("VOICE_ERROR bad_name"); return;
    }
  if (!SD_MMC.exists("/voice") && !SD_MMC.mkdir("/voice")) { Serial.println("VOICE_ERROR mkdir"); return; }
  snprintf(voice_upload_path, sizeof voice_upload_path, "/voice/%s.pcm", name);
  snprintf(voice_upload_temp, sizeof voice_upload_temp, "/voice/%s.tmp", name);
  if (SD_MMC.exists(voice_upload_path)) {
    File existing = SD_MMC.open(voice_upload_path, FILE_READ);
    if (!existing) { Serial.println("VOICE_ERROR existing_open"); return; }
    uint8_t buffer[256]; uint32_t existing_crc = 0xffffffffU, existing_size = 0;
    while (existing.available()) {
      size_t n = existing.read(buffer, sizeof buffer);
      if (!n) break;
      existing_size += n; existing_crc = voice_crc_update(existing_crc, buffer, n); yield();
    }
    existing.close();
    Serial.printf("VOICE_EXISTS %lu %08lx\n", static_cast<unsigned long>(existing_size), static_cast<unsigned long>(existing_crc ^ 0xffffffffU)); return;
  }
  if (SD_MMC.exists(voice_upload_temp)) { Serial.println("VOICE_ERROR temp_exists"); return; }
  voice_upload_file = SD_MMC.open(voice_upload_temp, FILE_WRITE);
  if (!voice_upload_file) { Serial.println("VOICE_ERROR open"); return; }
  voice_upload_size = size; voice_upload_expected_crc = crc; voice_upload_received = 0;
  voice_upload_crc = 0xffffffffU; voice_upload_last_ms = millis(); voice_upload_last_ack_ms = millis(); voice_upload_active = true;
  Serial.println("VOICE_READY 128");
}

void voice_upload_poll() {
  if (millis() - voice_upload_last_ms > 5000) {
    Serial.printf("VOICE_PROGRESS %lu\n", static_cast<unsigned long>(voice_upload_received));
    voice_upload_abort("timeout"); return;
  }
  if (voice_upload_received && voice_upload_received % 128 == 0 && millis() - voice_upload_last_ack_ms >= 500) {
    Serial.printf("VOICE_ACK %lu\n", static_cast<unsigned long>(voice_upload_received));
    voice_upload_last_ack_ms = millis();
  }
  uint8_t buf[256];
  while (Serial.available() && voice_upload_active) {
    size_t remaining = voice_upload_size - voice_upload_received;
    size_t block_remaining = 128 - voice_upload_received % 128;
    size_t n = min(static_cast<size_t>(Serial.available()), min(sizeof buf, min(remaining, block_remaining)));
    n = Serial.read(buf, n);
    if (!n) break;
    if (voice_upload_file.write(buf, n) != n) { voice_upload_abort("write"); return; }
    voice_upload_crc = voice_crc_update(voice_upload_crc, buf, n);
    voice_upload_received += n; voice_upload_last_ms = millis();
    if (voice_upload_received == voice_upload_size) {
      voice_upload_file.flush(); voice_upload_file.close();
      if ((voice_upload_crc ^ 0xffffffffU) != voice_upload_expected_crc) { voice_upload_abort("crc"); return; }
      File check = SD_MMC.open(voice_upload_temp, FILE_READ);
      uint32_t check_crc = 0xffffffffU, checked = 0;
      if (!check) { voice_upload_abort("readback_open"); return; }
      while (check.available()) {
        size_t count = check.read(buf, sizeof buf);
        if (!count) break;
        checked += count; check_crc = voice_crc_update(check_crc, buf, count);
        yield();
      }
      check.close();
      if (checked != voice_upload_size || (check_crc ^ 0xffffffffU) != voice_upload_expected_crc) { voice_upload_abort("readback"); return; }
      if (!SD_MMC.rename(voice_upload_temp, voice_upload_path)) { voice_upload_abort("rename"); return; }
      voice_upload_active = false; Serial.printf("VOICE_DONE %lu\n", static_cast<unsigned long>(checked)); return;
    }
    if (voice_upload_received % 128 == 0) {
      Serial.printf("VOICE_ACK %lu\n", static_cast<unsigned long>(voice_upload_received));
      voice_upload_last_ack_ms = millis();
    }
  }
}

void serial_command() {
  if (voice_upload_active) { voice_upload_poll(); return; }
  if (!Serial.available()) return;
  static char line[96] = {};
  static int length = 0;
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[length] = 0;
      int y, mo, d, h, mi;
      if (sscanf(line, "clock %d %d %d %d %d", &y, &mo, &d, &h, &mi) == 5) {
        datetime_t value{static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                         0, static_cast<uint8_t>(h), static_cast<uint8_t>(mi), 0};
        if (valid_time(value)) {
          clock_edit = value; save_clock(); Serial.println("clock ok");
        } else Serial.println("bad clock");
      } else if (!strncmp(line, "voice-put ", 10)) {
        voice_upload_begin(line);
        length = 0;
        return;
      } else if (!strcmp(line, "date-list")) {
        for (int i=0;i<lab.sample_count;++i) {
          const Sample& sample=lab.samples[i];
          Serial.printf("SAMPLE id=%u name=%s date=%d created=%lld\n",sample.id,sample.name,sample_date_key(sample),sample.created);
        }
      } else if (!strcmp(line, "run-list")) {
        for (int i=0;i<lab.run_count;++i) {
          const Run& run=lab.runs[i];
          Serial.printf("RUN index=%d id=%u sample_id=%u phase=%u name=%s steps=%d\n",i,run.id,run.sample_id,static_cast<unsigned>(run.phase),run.name,run.count);
          for (int s=0;s<run.count;++s) Serial.printf(" STEP index=%d started=%lld done=%lld skipped=%lld\n",s,run.steps[s].started,run.steps[s].done,run.steps[s].skipped);
        }
      } else if (!strcmp(line,"guard-test")) {
        Serial.println(pocket_guard_selftest() ? "GUARD PASS 9" : "GUARD FAIL");
      } else if (!strcmp(line,"stopwatch-test")) {
        Serial.println(stopwatch_selftest()?"STOPWATCH PASS":"STOPWATCH FAIL");
      } else if (!strcmp(line,"record-press-test")) {
        Serial.println(record_press_selftest()?"RECORD_PRESS PASS":"RECORD_PRESS FAIL");
      } else if (!strcmp(line,"record-beep-test")) {
        if(!voice_playing&&alert_run<0){record_audio_test_until=millis()+500;record_tone_remaining=2205;record_tone_phase=0;Serial.println("RECORD_BEEP test_started");}
      } else if (!strcmp(line,"stopwatch-store-test")) {
        bool busy=alert_run>=0||voice_playing||voice_upload_active||transfer_active()||stopwatch.running;
        for(int i=0;i<lab.run_count;++i)busy=busy||history_run_busy(lab.runs[i],now_ms());
        if(busy)Serial.println("STOPWATCH_STORE unavailable");
        else Serial.println(stopwatch_store_selftest()?"STOPWATCH_STORE PASS 150_records":"STOPWATCH_STORE FAIL");
      } else if (!strcmp(line,"stopwatch-status")) {
        Serial.printf("STOPWATCH running=%d elapsed_ms=%llu\n",stopwatch.running,
          static_cast<unsigned long long>(stopwatch.elapsed_us(esp_timer_get_time())/1000));
      } else if (!strcmp(line,"history-test")) {
        Serial.println(history_selftest()?"HISTORY_TEST PASS":"HISTORY_TEST FAIL");
      } else if (!strcmp(line,"history-e2e-test")) {
        if(alert_run>=0||voice_playing||voice_upload_active||transfer_active()||!clock_ok)Serial.println("HISTORY_E2E unavailable");
        else {uint32_t fixture=0;const char* error=history_e2e_test(now_ms(),&fixture);Serial.printf("HISTORY_E2E %s%s%s stack_free=%u fixture=%u\n",error?"FAIL":"PASS",error?" ":"",error?error:"",unsigned(uxTaskGetStackHighWaterMark(nullptr)),fixture);}
      } else if (!strncmp(line,"history-clean-fixture ",22)) {
        unsigned long id=0;char extra=0;
        if(sscanf(line,"history-clean-fixture %lu %c",&id,&extra)!=1)Serial.println("HISTORY_FIXTURE invalid");
        else Serial.printf("HISTORY_FIXTURE %s\n",history_cleanup_fixture(static_cast<uint32_t>(id))?"clean":"failed");
      } else if (!strncmp(line,"history-clean-test ",19)) {
        unsigned long id=0;char extra=0;
        if(sscanf(line,"history-clean-test %lu %c",&id,&extra)!=1)Serial.println("HISTORY_CLEAN invalid");
        else {const char* error=history_cleanup_aborted(static_cast<uint32_t>(id));Serial.printf("HISTORY_CLEAN %s%s%s\n",error?"FAIL":"PASS",error?" ":"",error?error:"");}
      } else if (!strcmp(line,"history-policy-test")) {
        const int64_t start=1793590859891LL,day=86400000LL;
        Run test{};test.count=1;step_clear(&test.steps[0],"水浴",1,RemindMode::Alarm);
        test.steps[0].started=start;
        bool ok=!history_age_reached(start,start+3*day-1,0,3)&&history_age_reached(start,start+3*day,0,3)&&
          !history_age_reached(start,start+7*day-1,0,7)&&history_age_reached(start,start+7*day,0,7)&&
          !history_age_reached(0,start+7*day,0,7)&&!history_age_reached(start,start-1,0,3)&&
          !history_age_reached(start,start+7*day,start+8*day,7)&&history_run_busy(test,start+1000)&&
          history_run_busy(test,start+60000);
        test.steps[0].notified=true;ok=ok&&!history_run_busy(test,start+60000);
        test.steps[0].notified=false;test.steps[0].mode=RemindMode::None;
        ok=ok&&history_run_busy(test,start+59999)&&!history_run_busy(test,start+60000);
        test.steps[0].minutes=0;ok=ok&&!history_run_busy(test,start+1);
        Serial.println(ok?"HISTORY_POLICY PASS":"HISTORY_POLICY FAIL");
      } else if (!strncmp(line,"voice-test ",11)) {
        unsigned long id = 0; char extra = 0;
        if (sscanf(line,"voice-test %lu %c", &id, &extra) != 1 || alert_run >= 0 || voice_playing) Serial.println("VOICE_TEST unavailable");
        else {
          int index = lab_find_run(static_cast<uint32_t>(id));
          if (index < 0 || lab.runs[index].count < 1) Serial.println("VOICE_TEST no_run");
          else Serial.printf("VOICE_TEST %s\n", voice_start(lab.runs[index], lab.runs[index].steps[0]) ? "started" : "failed");
        }
      } else if (!strcmp(line,"lab-codec-test")) {
        uint8_t* packed = static_cast<uint8_t*>(malloc(sizeof(LabData) * 2));
        uint8_t* decoded = static_cast<uint8_t*>(malloc(sizeof(LabData)));
        bool ok = packed && decoded;
        size_t size = 0;
        if (ok) {
          size = lab_pack(reinterpret_cast<const uint8_t*>(&lab), sizeof lab, packed, sizeof(LabData) * 2);
          ok = size && lab_unpack(packed, size, decoded, sizeof lab) && !memcmp(decoded, &lab, sizeof lab);
          const uint8_t malformed[] = { 0x02, 0x01 };
          uint8_t small[4] = {};
          ok = ok && !lab_unpack(malformed, sizeof malformed, small, sizeof small);
          const uint8_t overflow[] = { 0xff };
          ok = ok && !lab_unpack(overflow, sizeof overflow, small, sizeof small);
        }
        free(packed); free(decoded);
        Serial.printf("lab-codec-test %s raw=%u packed=%u\n", ok ? "PASS" : "FAIL", unsigned(sizeof lab), unsigned(size));
      } else if (!strcmp(line,"lock")) {
        lock_display();
      } else if (!strcmp(line,"status")) {
        const int steps = selected_template >= 0 && selected_template < templates.count
                            ? templates.items[selected_template].count : -1;
        uint8_t input_reg = IO_EXTENSION_IO_INPUT_ADDR, input_value = 0;
        const esp_err_t input_result = i2c_master_transmit_receive(IO_EXTENSION.addr, &input_reg, 1, &input_value, 1, 100);
        const int backlight_pin = input_result == ESP_OK ? ((input_value >> IO_EXTENSION_IO_2) & 1) : -1;
        uint8_t output_reg = IO_EXTENSION_IO_OUTPUT_ADDR, output_value = 0;
        uint8_t pwm_reg = IO_EXTENSION_PWM_ADDR, pwm_value = 0;
        const esp_err_t output_result = i2c_master_transmit_receive(IO_EXTENSION.addr, &output_reg, 1, &output_value, 1, 100);
        const esp_err_t pwm_result = i2c_master_transmit_receive(IO_EXTENSION.addr, &pwm_reg, 1, &pwm_value, 1, 100);
        Serial.printf("samples=%d runs=%d templates=%d steps=%d page=%d selected=%d light_off=%d dimmed=%d idle_ms=%u flash_dim=%d types_ok=%d storage=%d generation=%u rtc=%d free_heap=%u irq=%u presses=%u wakes=%u bl_pin=%d bl_mask=%u out_reg=%d pwm_reg=%d alarm_volume=%d sound_settings=%d voice_sd=%d battery_mv=%d battery_pct=%d battery_valid=%d battery_raw=%u schema=3 locked=%d\n",
                      lab.sample_count, lab.run_count, templates.count, steps,
                      static_cast<int>(page), selected_template, backlight_off, dimmed,
                      millis() - last_input_ms,
                      screen_hidden_for_flash, templates_ok, storage_ok,
                      lab_generation(), clock_ok, ESP.getFreeHeap(), touch_irq_count,
                      touch_press_count, wake_attempt_count, backlight_pin, IO_EXTENSION.Last_io_value,
                      output_result == ESP_OK ? output_value : -1, pwm_result == ESP_OK ? pwm_value : -1,
                      alarm_volume, sound_settings_ready, voice_sd_ready, static_cast<int>(lroundf(battery_voltage*1000)), -1, battery_valid, battery_raw, screen_locked);
      } else if (!strcmp(line, "wake")) {
        wake_display(millis());
        Serial.println("wake ok");
      } else if (!strcmp(line, "backlight-reinit")) {
        IO_EXTENSION_IO_Mode(0xff);
        wake_display(millis());
        Serial.println("backlight-reinit ok");
      } else if (!strcmp(line, "beep-test")) {
        alarm_melody();
        Serial.println("beep-test done");
      } else if (!strcmp(line,"ui-hits")) {
        Serial.printf("UI_HITS page=%d count=%d\n",int(page),hit_count);
        for(int i=0;i<hit_count;++i){const auto& h=hits[i];Serial.printf("HIT %d %d %d %d action=%d arg=%d\n",h.x,h.y,h.w,h.h,int(h.action),h.arg);}
      } else if (!strcmp(line,"results-status")) {
        Serial.printf("results_ready=%d record_size=%u free_entries=%u heap=%u\n",results_storage_ok(),unsigned(sizeof(ResultRecord)),results_free_entries(),ESP.getFreeHeap());
      } else if (!strcmp(line,"results-test")) {
        int errors=results_math_selftest();bool saved=result_store_selftest();
        Serial.printf("results-test %s math_errors=%d isolated_nvs=%d\n",!errors&&saved?"PASS":"FAIL",errors,saved);
      } else if (!strcmp(line,"results-reboot-prepare")) {
        Serial.printf("results-reboot-prepare %s\n",result_persistence_probe(true)?"PASS":"FAIL");
      } else if (!strcmp(line,"results-reboot-check")) {
        Serial.printf("results-reboot-check %s\n",result_persistence_probe(false)?"PASS":"FAIL");
      } else if (!strcmp(line,"results-fixture")) {
        Serial.println(result_fixture_json());
      } else if (!strcmp(line,"results-dump")) {
        for(int i=0;i<lab.run_count;++i){String text=result_json(lab.runs[i]);Serial.printf("RESULT run=%u %s\n",lab.runs[i].id,text.length()?text.c_str():"unrecorded");}
      } else if (!strcmp(line,"export-test")) {
        uint32_t mask=lab.run_count==32?0xffffffffu:(uint32_t(1)<<lab.run_count)-1;
        bool ok=result_export(mask,true,exported_path);Serial.printf("export-test %s path=%s\n",ok?"PASS":"FAIL",exported_path.c_str());
      } else if (!strcmp(line,"export-read")) {
        String report=transfer_report_path();if(!report.length())report=exported_path;
        File f=SD_MMC.open(report,FILE_READ);Serial.printf("EXPORT_BEGIN bytes=%u\n",f?unsigned(f.size()):0);
        if(f){while(f.available())Serial.write(f.read());f.close();}Serial.println("EXPORT_END");
      } else if (!strcmp(line,"mobile-html-test")) {
        uint32_t mask=lab.run_count==32?0xffffffffu:(uint32_t(1)<<lab.run_count)-1;
        String html=result_mobile_html(mask);Serial.printf("HTML_BEGIN bytes=%u\n",unsigned(html.length()));Serial.println(html);Serial.println("HTML_END");
      } else if (!strcmp(line,"transfer-start")) {
        bool ok=exported_path.length()&&transfer_start(exported_path);
        if(ok){qr_wifi=true;prepare_transfer_qr();redraw=true;}
        Serial.printf("transfer=%d ssid=%s password=%s url=%s\n",ok,transfer_ssid(),transfer_password(),transfer_address().c_str());
      } else if (!strcmp(line,"transfer-test")) {
        Serial.printf("transfer-test %s\n",transfer_selftest()?"PASS":"FAIL");
      } else if (!strcmp(line,"transfer-stop")) {
        transfer_stop();redraw=true;Serial.println("transfer stopped");
      } else if (!strcmp(line,"transfer-status")) {
        Serial.printf("transfer=%d downloads=%u bytes=%u/%u heap=%u\n",transfer_active(),transfer_downloads(),transfer_sent(),transfer_size(),ESP.getFreeHeap());
      } else if (!strcmp(line,"records-export")) {
        records_force();Serial.println(records_sync()?"records-export PASS":"records-export FAIL");
      } else if (!strcmp(line,"records-check")) {
        records_print();
      } else if (!strcmp(line,"records-test")) {
        Serial.println(records_selftest()?"records-test PASS":"records-test FAIL");
      } else if (!strcmp(line,"records-status")) {
        Serial.printf("records_ok=%d records_generation=%u lab_generation=%u\n",records_ok(),records_generation(),lab_generation());
      } else if (!strcmp(line, "voice-list")) {
        serial_voice_list();
      } else if (!strcmp(line, "save-check")) {
        Serial.println(lab_save() ? "save ok" : "save failed");
      } else Serial.println("commands: status | wake | beep-test | save-check | clock YYYY MM DD HH MM");
      length = 0;
    } else if (length < static_cast<int>(sizeof line) - 1) line[length++] = ch;
  }
}

void setup() {
  Serial.begin(115200);
  setenv("TZ", "CST-8", 1);
  tzset();
  DEV_I2C_Init();
  IO_EXTENSION_Init();
  touch_gt911_init(DEV_I2C_Get_Bus_Device());
  attachInterrupt(digitalPinToInterrupt(EXAMPLE_PIN_NUM_TOUCH_INT), touch_irq_handler, FALLING);
  PCF85063A_Init();
  sync_from_rtc();
  storage_ok = lab_load();
  if(storage_ok && !run_age_init())Serial.println("Project date storage unavailable");
  if (storage_ok) sound_settings_load();
  if(storage_ok && !results_load())Serial.println("Results NVS unavailable; timer data preserved");
  templates_ok = storage_ok && templates_load();
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);
  if (SD_MMC.setPins(12, 11, 13)) voice_sd_ready = SD_MMC.begin("/sdcard", true, false);
  Serial.printf("voice_sd=%d\n", voice_sd_ready);
  if(storage_ok) {records_begin(voice_sd_ready);history_recover();}
  waveshare_esp32_s3_rgb_lcd_init();
  void* a = nullptr;
  void* b = nullptr;
  waveshare_get_frame_buffer(&a, &b);
  frame_a = static_cast<uint16_t*>(a);
  frame_b = static_cast<uint16_t*>(b);
  pixels = frame_a;
  apply_brightness(brightness);
  last_input_ms = millis();
  if (!frame_a || !frame_b) Serial.println("RGB framebuffers unavailable");
  else draw_all();
  display_ready = frame_a && frame_b;
  Serial.printf("Asphalt Timer 4.3C: samples=%d runs=%d templates=%d storage=%d rtc=%d psram=%u\n",
                lab.sample_count, lab.run_count, templates.count, storage_ok, clock_ok, ESP.getPsramSize());
}

void loop() {
  serial_command();
  const uint32_t tick = millis();
  poll_battery(tick);
  transfer_tick();
  if(page==Page::Transfer && tick-last_draw_ms>=1000)redraw=true;
  static uint32_t last_touch_poll=0;
  const bool quick_record=page==Page::Stopwatch&&!screen_locked&&!backlight_off;
  const uint32_t irq_snapshot=touch_irq_count;
  if(irq_snapshot!=last_touch_irq_count || tick-last_touch_poll>=uint32_t(quick_record?10:20)) {
  last_touch_poll=tick;
  last_touch_irq_count=irq_snapshot;
  touch_gt911_point_t point=touch_gt911_read_point(2);
  const uint64_t touch_at=esp_timer_get_time();
  const bool record_enabled=quick_record&&alert_run<0&&confirm_kind==ConfirmKind::None;
  if(record_press.observe(point.fresh,point.cnt,point.cnt?point.x[0]:0,point.cnt?point.y[0]:0,record_enabled))stopwatch_primary_at(touch_at);
  // A poll without a new GT911 frame is neither a press nor a release.
  if(point.fresh) {
  const bool pressed=point.cnt>0;
  if (screen_locked || backlight_off) {
    // Interrupts, taps, long contact and pocket movement never directly wake.
    if (unlock_guard.observe(point.cnt,pressed?point.x[0]:0,pressed?point.y[0]:0,tick)) {
      screen_locked=false;
      touch_down=false;
      wake_only=true;
      wake_display(tick);
      Serial.println("UNLOCK swipe");
    }
    touch_down=false;
  } else {
    if (point.cnt>1) {
      touch_down=false;
      wake_only=true;
    } else if (pressed && !touch_down) {
      ++touch_press_count;
      last_input_ms=tick;
      touch_begin_ms=tick;touch_samples=1;
      touch_start_x=touch_last_x=point.x[0];
      touch_start_y=touch_last_y=point.y[0];
      wake_only=false;
    } else if (pressed) {
      touch_last_x=point.x[0];touch_last_y=point.y[0];
      if(touch_samples<255)++touch_samples;
    } else if (touch_down) {
      const uint32_t duration=tick-touch_begin_ms;
      if (!wake_only && touch_samples>=1 && duration>=10) {
        const int dx=touch_last_x-touch_start_x,dy=touch_last_y-touch_start_y;
        if(touch_start_y<420 && abs(dy)>=55 && abs(dy)>abs(dx)) handle_swipe(dy);
        else if(abs(dx)<30 && abs(dy)<30 && duration<=1500)handle_touch(touch_start_x,touch_start_y);
      }
    }
    touch_down=pressed && point.cnt==1;
  }

  } // Missing controller frames must not cancel a tap or an unlock swipe.
  } // Touch cadence is independent of the network/audio service loop.

  if(record_flash_until&&int32_t(tick-record_flash_until)>=0){record_flash_until=0;redraw=true;}
  record_feedback_tick(millis());

  if (wake_reassert_ms && static_cast<int32_t>(tick - wake_reassert_ms) >= 0) {
    wake_reassert_ms = 0;
    if (!backlight_off) { apply_brightness(brightness); redraw = true; }
  }
  if (type_save_pending && static_cast<int32_t>(tick - type_save_due_ms) >= 0) {
    type_save_pending = false;
    if (!templates_save()) notice("试验类型保存失败");
  }
  if (lab_save_pending && static_cast<int32_t>(tick - lab_save_due_ms) >= 0) {
    lab_save_pending = false;
    if (!lab_save()) notice("项目时间保存失败");
  }
  if (toast[0] && static_cast<int32_t>(tick - toast_until_ms) >= 0) {
    toast[0] = 0;
    redraw = true;
  }
  if (tick - last_reminder_check_ms >= 250) {
    check_reminders(tick);
    last_reminder_check_ms = tick;
  }
  history_auto_tick(tick);
  voice_tick();
  static uint32_t last_records_retry=0;
  if(storage_ok && !voice_playing && !transfer_active() && !records_ok() && tick-last_records_retry>=30000) {
    last_records_retry=tick;
    // Reopen the same card without formatting it; retry after removal or a full-card error.
    SD_MMC.end();
    voice_sd_ready=SD_MMC.begin("/sdcard",true,false);
    records_begin(voice_sd_ready);
    history_recover();
    redraw=true;
  }
  if (voice_failed) { voice_failed = false; alarm_melody(); }
  power_tick(tick);
  if (sound_save_pending && (page != Page::Settings || backlight_off)) {
    sound_save_pending = false;
    if (!sound_settings_save()) notice("闹铃音量保存失败");
  }
  const uint32_t refresh_interval = page==Page::Stopwatch && stopwatch.running && !screen_locked ? 50 : live_display_needed() ? 1000 : 60000;
  if (!backlight_off && (redraw || tick - last_draw_ms >= refresh_interval)) draw_all();
  // Leave enough time for 20 ms touch polling on ordinary lit pages.
  delay(voice_playing || voice_upload_active ? 0 : record_tone_remaining>0 ? 1 : quick_record ? 2 : transfer_active() ? 2 : backlight_off || screen_locked ? 20 : 10);
}
