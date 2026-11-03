#include "template_store.hpp"
#include "flash_guard.hpp"

#include <Arduino.h>
#include <nvs.h>
#include <string.h>

TemplateData templates{};

namespace {
constexpr uint32_t kMagic = 0x33504D54;  // TMP3
constexpr char kPartition[] = "timer_nvs";
constexpr char kNamespace[] = "labtypes";
constexpr char kA[] = "types_a";
constexpr char kB[] = "types_b";

struct Record {
  uint32_t magic;
  uint32_t generation;
  uint32_t checksum;
  TemplateData data;
};

static Record a{}, b{};
static nvs_handle_t handle = 0;
static uint32_t generation = 0;
static bool ready = false;

uint32_t hash_data(const TemplateData& data) {
  uint32_t hash = 2166136261u;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
  for (size_t i = 0; i < sizeof data; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

bool read_one(const char* key, Record& record) {
  size_t size = sizeof record;
  if (nvs_get_blob(handle, key, &record, &size) != ESP_OK || size != sizeof record) return false;
  if (record.magic != kMagic || record.data.count > kMaxTemplates ||
      record.checksum != hash_data(record.data)) return false;
  for (int i = 0; i < record.data.count; ++i)
    if (record.data.items[i].count < 1 || record.data.items[i].count > kMaxSteps) return false;
  return true;
}

void set_step(LabTemplate& item, int index, const char* name, int minutes, RemindMode mode) {
  TemplateStep& step = item.steps[index];
  memset(&step, 0, sizeof step);
  strncpy(step.name, name, sizeof(step.name) - 1);
  step.minutes = minutes;
  step.mode = mode;
}

void add_default(const char* name, const char* step1, int minutes1, RemindMode mode1,
                 const char* step2 = nullptr, int minutes2 = 0, RemindMode mode2 = RemindMode::None,
                 const char* step3 = nullptr, int minutes3 = 0, RemindMode mode3 = RemindMode::None) {
  LabTemplate& item = templates.items[templates.count++];
  memset(&item, 0, sizeof item);
  item.id = templates.next_id++;
  strncpy(item.name, name, sizeof(item.name) - 1);
  item.count = step3 ? 3 : step2 ? 2 : 1;
  set_step(item, 0, step1, minutes1, mode1);
  if (step2) set_step(item, 1, step2, minutes2, mode2);
  if (step3) set_step(item, 2, step3, minutes3, mode3);
}

void defaults() {
  memset(&templates, 0, sizeof templates);
  templates.next_id = 1;
  add_default("针入度", "室温冷却", 90, RemindMode::Notification,
              "水浴", 90, RemindMode::Notification, "测定与录入读数");
  add_default("样品加热 30 分", "加热", 30, RemindMode::Alarm);
  add_default("样品加热 60 分", "加热", 60, RemindMode::Alarm);
  add_default("样品加热 120 分", "加热", 120, RemindMode::Alarm);
  add_default("软化点", "样品准备", 0, RemindMode::None, "测定与录入读数");
  add_default("60 ℃动力黏度", "样品准备", 0, RemindMode::None, "测定与录入读数");
  add_default("135 ℃旋转黏度", "样品准备", 0, RemindMode::None, "测定与录入读数");
}
}  // namespace

bool templates_load() {
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  ready = true;
  const bool va = read_one(kA, a), vb = read_one(kB, b);
  if (va || vb) {
    const Record& selected = vb && (!va || b.generation > a.generation) ? b : a;
    templates = selected.data;
    generation = selected.generation;
    return true;
  }
  defaults();
  return templates_save();
}

bool templates_save() {
  if (!ready) return false;
  const uint32_t next = generation + 1;
  Record& record = next & 1 ? a : b;
  record.magic = kMagic;
  record.generation = next;
  record.data = templates;
  record.checksum = hash_data(record.data);
  screen_before_flash();
  const esp_err_t write = nvs_set_blob(handle, next & 1 ? kA : kB, &record, sizeof record);
  const esp_err_t committed = write == ESP_OK ? nvs_commit(handle) : write;
  screen_after_flash();
  if (write != ESP_OK || committed != ESP_OK) return false;
  generation = next;
  return true;
}

int templates_add() {
  if (!ready || templates.count >= kMaxTemplates) return -1;
  const int index = templates.count++;
  LabTemplate& item = templates.items[index];
  memset(&item, 0, sizeof item);
  item.id = templates.next_id++;
  strncpy(item.name, "新实验类型", sizeof(item.name) - 1);
  item.count = 1;
  set_step(item, 0, "新步骤", 0, RemindMode::None);
  if (templates_save()) return index;
  --templates.count; --templates.next_id;
  memset(&item, 0, sizeof item);
  return -1;
}

bool templates_remove(int index) {
  if (!ready || index < 0 || index >= templates.count) return false;
  for (int i = index; i < templates.count - 1; ++i) templates.items[i] = templates.items[i + 1];
  --templates.count;
  memset(&templates.items[templates.count], 0, sizeof(LabTemplate));
  return templates_save();
}
