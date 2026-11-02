#include <Arduino.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <nvs.h>
#include <SD_MMC.h>
#include "src/rgb_lcd_port/rgb_lcd_port.h"
#include "src/gt911/gt911.h"
#include "src/rtc_pcf85063a/rtc_pcf85063a.h"
#include "src/speaker_microphone/codec_dev.h"
#include "src/speaker_microphone/speaker_microphone.h"
#include "src/font_draw.hpp"
#include "src/lab_store.hpp"
#include "src/pocket_guard.hpp"
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
  Templates, TemplateEdit, EditText, Clock, Settings, SampleDates, SampleRecords, StepRecord, Results, ResultOptions, NumericPad, ResultSummary, ExportSelect, Transfer, ResultStandards, PageJump, ExportDates, ExportSamples
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
  ExportDateOpen, ExportSampleOpen, ExportDateToggle, ExportSampleToggle, ExportRoot
};
struct Hit { int x, y, w, h; Action action; int arg; };
Hit hits[90];
int hit_count = 0;
Page page = Page::Dashboard;
int selected_sample = -1, selected_run = -1, selected_template = -1;
int selected_step = 0, list_page = 0, step_page = 0;
Page jump_parent=Page::Dashboard;
int jump_total=1;
char jump_entry[4]={};
int task_tab = 0;  // 0: active timers, 1: due timers.
int pending_run = -1, pending_step = -1;
enum class ConfirmKind : uint8_t { None, Skip, Early, DeleteSample, DeleteRun, DeleteTemplate, DeleteStep };
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
int battery_percent = 0;
bool battery_valid = false;
bool battery_settling = false;
uint32_t battery_settle_ms = 0;
float battery_settle_voltage = 0;
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
int clock_field = 0;
datetime_t clock_edit{};
char toast[80] = {};
uint32_t toast_until_ms = 0;

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
// Voltage-derived state of charge is approximate, especially during charging/load.
int battery_percent_for_voltage(float volts) {
  const float voltage[] = {3.00f,3.30f,3.50f,3.60f,3.70f,3.80f,3.90f,4.00f,4.10f,4.20f};
  const int percent[] = {0,5,10,20,40,60,75,85,95,100};
  if (volts <= voltage[0]) return 0;
  for (int i=1;i<10;++i) {
    if (volts <= voltage[i]) {
      const float fraction=(volts-voltage[i-1])/(voltage[i]-voltage[i-1]);
      return constrain(static_cast<int>(lroundf(percent[i-1]+fraction*(percent[i]-percent[i-1]))),0,100);
    }
  }
  return 100;
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
    if (++failures>=3 && battery_valid) { battery_valid=false; redraw=true; }
    return;
  }
  failures=0;
  sum+=raw;
  if (++count<10) return;
  const bool was_settling=battery_settling;
  const bool had_value=battery_valid;
  const int previous_percent=battery_percent;
  const float previous_voltage=battery_voltage;
  battery_raw=static_cast<uint16_t>((sum+count/2)/count);
  const float measured=(sum/static_cast<float>(count))*(3.0f*3.3f/1023.0f);
  battery_voltage=battery_valid ? 0.75f*battery_voltage+0.25f*measured : measured;
  if (had_value && fabsf(measured-previous_voltage)>=0.08f && !battery_settling) {
    battery_settling=true; battery_settle_ms=tick; battery_settle_voltage=measured;
  }
  if (battery_settling) {
    if (fabsf(measured-battery_settle_voltage)>=0.025f) {
      battery_settle_voltage=measured; battery_settle_ms=tick;
    }
    if (static_cast<uint32_t>(tick-battery_settle_ms)>=15000) battery_settling=false;
  }
  if (!battery_settling) battery_percent=battery_percent_for_voltage(battery_voltage);
  battery_valid=true;
  sum=0; count=0;
  if (!backlight_off && (!had_value || was_settling!=battery_settling || battery_percent!=previous_percent || fabsf(battery_voltage-previous_voltage)>=0.01f)) redraw=true;
  // ADC updates never wake the screen.
}

void draw_battery() {
  constexpr int x=684, y=12, w=35, h=18;
  const uint16_t color=!battery_valid ? MUTED : battery_percent<=15 ? RED : battery_percent<=30 ? ORANGE : GREEN;
  box(674,4,126,56,CARD);
  box(x,y,w,h,color);
  box(x+2,y+2,w-4,h-4,CARD);
  box(x+w,y+5,3,8,color);
  if (battery_valid) box(x+4,y+4,(w-8)*battery_percent/100,h-8,color);
  char level[12], volts[16];
  if (battery_valid) {
    if (battery_settling) snprintf(level,sizeof level,"...");
    else snprintf(level,sizeof level,"~%d%%",battery_percent);
    snprintf(volts,sizeof volts,"%.2fV",battery_voltage);
  } else {
    snprintf(level,sizeof level,"--%%");
    snprintf(volts,sizeof volts,"--.--V");
  }
  label(728,8,level,color);
  label(692,35,volts,MUTED);
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
  button(166, 428, 150, 48, "样品", Action::Samples, 0, (page == Page::Samples || page == Page::SampleDates || page == Page::Sample || page == Page::SampleRecords || page == Page::StepRecord || page == Page::Results || page == Page::ResultOptions || page == Page::ResultStandards || page == Page::NumericPad || page == Page::ResultSummary) ? CYAN : BLUE);
  button(324, 428, 150, 48, "试验类型", Action::OpenTemplates, 0,
         page == Page::Templates || page == Page::TemplateEdit ? CYAN : BLUE);
  button(482, 428, 150, 48, "设置", Action::OpenSettings, 0,
         page == Page::Settings || page == Page::Clock ? CYAN : BLUE);
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
  audio_begin();
  if (!audio_ready) return false;
  esp_codec_dev_sample_info_t sample_info{};
  sample_info.bits_per_sample = 16;
  sample_info.channel = 2;
  sample_info.sample_rate = 44100;
  const int opened = esp_codec_dev_open(play_device, &sample_info);
  if (opened != ESP_OK) { Serial.printf("speaker open failed: %d\n", opened); return false; }
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
  if (!voice_sd_ready) return false;
  char path[40];
  snprintf(path, sizeof path, "/voice/%016llx.pcm", static_cast<unsigned long long>(voice_key(run.name, step.name)));
  voice_file = SD_MMC.open(path, FILE_READ);
  if (!voice_file) return false;
  const size_t size = voice_file.size();
  if (size == 0 || size > 44100 * 4 * 48 / 10 || (size % 4) != 0) {
    voice_file.close();
    return false;
  }
  if (alarm_volume == 0) { voice_file.close(); return true; }
  if (!audio_open(alarm_volume)) { voice_file.close(); return false; }
  voice_remaining = size;
  voice_playing = true;
  return true;
}

void voice_stop() {
  if (!voice_playing) return;
  voice_playing = false;
  voice_remaining = 0;
  voice_file.close();
  audio_close();
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

void draw_sample_dates() {
  char clock_text[40]; format_clock(clock_text,sizeof clock_text);
  title_bar("样品记录",clock_ok ? clock_text : "时钟未设置");
  label(22,89,"按建立日期分类",MUTED);
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
  page_control(list_page,max(1,(count+3)/4),300,75,260,46);
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
  if (!found) label(28, 181, "样品尚无实验项目", MUTED);
  page_control(list_page,max(1,(found+3)/4),588,75,194,46);
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
  label(338, 83, run.sample, MUTED);
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
  title_bar("设置时钟", clock_ok ? "RTC 已连接" : "RTC 时间未设置");
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

void draw_confirm() {
  if (confirm_kind == ConfirmKind::None) return;
  hit_count = 0;
  box(112, 96, 576, 291, BG);
  const char* message = confirm_kind == ConfirmKind::Skip ? "跳过前面未完成的步骤？" :
                        confirm_kind == ConfirmKind::Early ? "尚未到点，仍确认完成？" :
                        confirm_kind == ConfirmKind::DeleteSample ? "删除样品及全部项目？" :
                        confirm_kind == ConfirmKind::DeleteRun ? "删除这个实验项目？" :
                        confirm_kind == ConfirmKind::DeleteTemplate ? "删除试验类型？已有项目保留。" :
                        confirm_kind == ConfirmKind::DeleteStep ? "删除这个模板步骤？" : "";
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
  const LabTemplate& source = templates.items[template_index];
  Run run{};
  run.phase = project_phase;
  strncpy(run.name, source.name, sizeof(run.name) - 1);
  run.count = source.count;
  for (int i = 0; i < run.count; ++i)
    step_clear(&run.steps[i], source.steps[i].name, source.steps[i].minutes, source.steps[i].mode);
  const int index = lab_add_run(selected_sample, run);
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
      Run run{}; make_generic(run, entry, project_minutes); run.phase=project_phase;
      const int i = lab_add_run(selected_sample, run);
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
  if(page==Page::PageJump||page==Page::ResultStandards||page==Page::NumericPad||page==Page::Results||page==Page::ResultOptions||page==Page::Transfer)return;
  if(page==Page::ResultSummary){if(direction<0)summary_page=max(0,summary_page-1);redraw=true;return;}
  if(page==Page::ExportSelect)max_page=max(0,(export_sample_run_count()-1)/4);
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
    alert_run = i; alert_step = j;
    voice_attempted = false;
    alert_end_ms = tick + (step.mode == RemindMode::Alarm ? 30000 : 8000);
    alarm_sound_end_ms = tick + 5000;
    next_beep_ms = tick;
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

void serial_command() {
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
        Serial.printf("samples=%d runs=%d templates=%d steps=%d page=%d selected=%d light_off=%d dimmed=%d idle_ms=%u flash_dim=%d types_ok=%d storage=%d generation=%u rtc=%d free_heap=%u irq=%u presses=%u wakes=%u bl_pin=%d bl_mask=%u out_reg=%d pwm_reg=%d alarm_volume=%d sound_settings=%d voice_sd=%d battery_mv=%d battery_pct=%d battery_valid=%d battery_raw=%u battery_settling=%d schema=3 locked=%d\n",
                      lab.sample_count, lab.run_count, templates.count, steps,
                      static_cast<int>(page), selected_template, backlight_off, dimmed,
                      millis() - last_input_ms,
                      screen_hidden_for_flash, templates_ok, storage_ok,
                      lab_generation(), clock_ok, ESP.getFreeHeap(), touch_irq_count,
                      touch_press_count, wake_attempt_count, backlight_pin, IO_EXTENSION.Last_io_value,
                      output_result == ESP_OK ? output_value : -1, pwm_result == ESP_OK ? pwm_value : -1,
                      alarm_volume, sound_settings_ready, voice_sd_ready, static_cast<int>(lroundf(battery_voltage*1000)), battery_percent, battery_valid, battery_raw, battery_settling, screen_locked);
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
  if (storage_ok) sound_settings_load();
  if(storage_ok && !results_load())Serial.println("Results NVS unavailable; timer data preserved");
  templates_ok = storage_ok && templates_load();
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);
  if (SD_MMC.setPins(12, 11, 13)) voice_sd_ready = SD_MMC.begin("/sdcard", true, false);
  Serial.printf("voice_sd=%d\n", voice_sd_ready);
  if(storage_ok) records_begin(voice_sd_ready);
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
  if(tick-last_touch_poll>=uint32_t(screen_locked?20:40)) {
  last_touch_poll=tick;
  touch_gt911_point_t point=touch_gt911_read_point(2);
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
      if (!wake_only && touch_samples>=2 && duration>=60) {
        const int dx=touch_last_x-touch_start_x,dy=touch_last_y-touch_start_y;
        if(touch_start_y<420 && abs(dy)>=55 && abs(dy)>abs(dx)) handle_swipe(dy);
        else if(abs(dx)<30 && abs(dy)<30 && duration<=1500)handle_touch(touch_start_x,touch_start_y);
      }
    }
    touch_down=pressed && point.cnt==1;
  }

  } // Touch cadence is independent of the 2ms network service loop.

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
  voice_tick();
  static uint32_t last_records_retry=0;
  if(storage_ok && !voice_playing && !transfer_active() && !records_ok() && tick-last_records_retry>=30000) {
    last_records_retry=tick;
    // Reopen the same card without formatting it; retry after removal or a full-card error.
    SD_MMC.end();
    voice_sd_ready=SD_MMC.begin("/sdcard",true,false);
    records_begin(voice_sd_ready);
    redraw=true;
  }
  if (voice_failed) { voice_failed = false; alarm_melody(); }
  power_tick(tick);
  if (sound_save_pending && (page != Page::Settings || backlight_off)) {
    sound_save_pending = false;
    if (!sound_settings_save()) notice("闹铃音量保存失败");
  }
  const uint32_t refresh_interval = live_display_needed() ? 1000 : 60000;
  if (!backlight_off && (redraw || tick - last_draw_ms >= refresh_interval)) draw_all();
  delay(voice_playing ? 0 : transfer_active() ? 2 : screen_locked ? 20 : 40);
}
