#include "lab_store.hpp"
#include "flash_guard.hpp"
#include "tf_records.hpp"
#include "result_store.hpp"

#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

LabData lab{};

namespace {
constexpr uint32_t kMagic = 0x3342414c;  // LAB3
constexpr uint16_t kVersion = 3;
constexpr char kPartition[] = "timer_nvs";
constexpr char kNamespace[] = "lab4c";
constexpr char kSlot0[] = "lab3_0";
constexpr char kSlot1[] = "lab3_1";

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t generation;
  uint32_t checksum;
};

struct Record {
  Header header;
  LabData data;
};

static Record slot0{}, slot1{};  // Never allocate full records on the task stack.
static uint32_t generation = 0;
static nvs_handle_t handle = 0;
static bool storage_ok = false;

uint32_t checksum(const uint8_t* data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
  return hash;
}

// Version 2 layout is kept explicitly for non-destructive migration.
struct LegacyRun {
  uint32_t id;
  uint32_t sample_id;
  char sample[24];
  char name[24];
  Step steps[kMaxSteps];
  int count;
};
struct LegacyLabData {
  uint32_t next_id;
  uint16_t sample_count;
  uint16_t run_count;
  Sample samples[kMaxSamples];
  LegacyRun runs[kMaxRuns];
};
struct LegacyRecord { Header header; LegacyLabData data; };
static LegacyRecord legacy_record{};

bool valid_runs(const LabData& data) {
  for (int i=0;i<data.run_count;++i) {
    const Run& run=data.runs[i];
    if (run.count<1 || run.count>kMaxSteps || static_cast<unsigned>(run.phase)>2) return false;
  }
  return true;
}

bool migrate_legacy() {
  bool found=false;
  const char* legacy_keys[] = {"lab0", "lab1"};
  for (const char* key : legacy_keys) {
    size_t size=sizeof legacy_record;
    if (nvs_get_blob(handle,key,&legacy_record,&size)!=ESP_OK || size!=sizeof legacy_record) continue;
    const auto& old=legacy_record;
    if (old.header.magic!=0x3242414c || old.header.version!=2 || old.header.size!=sizeof(LegacyLabData) ||
        old.data.sample_count>kMaxSamples || old.data.run_count>kMaxRuns ||
        old.header.checksum!=checksum(reinterpret_cast<const uint8_t*>(&old.data),sizeof old.data)) continue;
    bool valid=true;
    for (int i=0;i<old.data.run_count;++i)
      if (old.data.runs[i].count<1 || old.data.runs[i].count>kMaxSteps) valid=false;
    if (!valid || (found && old.header.generation<=generation)) continue;
    memset(&lab,0,sizeof lab);
    lab.next_id=old.data.next_id;
    lab.sample_count=old.data.sample_count;
    lab.run_count=old.data.run_count;
    memcpy(lab.samples,old.data.samples,sizeof lab.samples);
    for (int i=0;i<lab.run_count;++i) {
      const LegacyRun& source=old.data.runs[i];
      Run& dest=lab.runs[i];
      dest.id=source.id; dest.sample_id=source.sample_id;
      memcpy(dest.sample,source.sample,sizeof dest.sample);
      memcpy(dest.name,source.name,sizeof dest.name);
      memcpy(dest.steps,source.steps,sizeof dest.steps);
      dest.count=source.count;
      dest.phase=AgingPhase::Original;
    }
    generation=old.header.generation;
    found=true;
  }
  return found;
}

bool record_exists(const char* key) {
  size_t size=0;
  return nvs_get_blob(handle,key,nullptr,&size)==ESP_OK;
}

bool read_slot(const char* key, Record& record) {
  size_t size = sizeof record;
  if (nvs_get_blob(handle, key, &record, &size) != ESP_OK || size != sizeof record) return false;
  return record.header.magic == kMagic && record.header.version == kVersion &&
         record.header.size == sizeof(LabData) &&
         record.data.sample_count <= kMaxSamples && record.data.run_count <= kMaxRuns && valid_runs(record.data) &&
         record.header.checksum == checksum(reinterpret_cast<const uint8_t*>(&record.data), sizeof(LabData));
}
}  // namespace

bool lab_load() {
  memset(&lab, 0, sizeof lab);
  lab.next_id = 1;
  const esp_err_t init = nvs_flash_init_partition(kPartition);
  if (init != ESP_OK && init != ESP_ERR_INVALID_STATE) {
    Serial.printf("timer_nvs init failed: %s\n", esp_err_to_name(init));
    return false;  // No automatic erase of the existing partition.
  }
  const esp_err_t opened = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
  if (opened != ESP_OK) {
    Serial.printf("timer_nvs open failed: %s\n", esp_err_to_name(opened));
    return false;
  }
  storage_ok = true;
  const bool valid0 = read_slot(kSlot0, slot0);
  const bool valid1 = read_slot(kSlot1, slot1);
  if (valid0 || valid1) {
    const bool use1 = valid1 && (!valid0 || slot1.header.generation > slot0.header.generation);
    const Record& selected = use1 ? slot1 : slot0;
    lab = selected.data;
    generation = selected.header.generation;
  }
  if (!valid0 && !valid1) {
    if (record_exists(kSlot0) || record_exists(kSlot1)) {
      Serial.println("LAB3 records invalid; refusing to overwrite");
      storage_ok=false; return false;
    }
    if (migrate_legacy()) {
      Serial.printf("Migrating LAB2: samples=%d runs=%d\n",lab.sample_count,lab.run_count);
      if (!lab.next_id) lab.next_id=1;
      if (!lab_save()) { storage_ok=false; return false; }
      Serial.println("LAB3 migration saved and verified");
      return true; // Old LAB2 records remain intact.
    }
    if (record_exists("lab0") || record_exists("lab1")) {
      Serial.println("LAB2 records invalid; refusing to overwrite");
      storage_ok=false; return false;
    }
  }
  if (!lab.next_id) lab.next_id = 1;
  return true;
}

bool lab_save() {
  if (!storage_ok) return false;
  const uint32_t next_generation = generation + 1;
  Record& record = (next_generation & 1) ? slot1 : slot0;
  record.header = {kMagic, kVersion, sizeof(LabData), next_generation,
                   checksum(reinterpret_cast<const uint8_t*>(&lab), sizeof lab)};
  memcpy(&record.data,&lab,sizeof lab);
  screen_before_flash();
  const esp_err_t write = nvs_set_blob(handle, (next_generation & 1) ? kSlot1 : kSlot0,
                                       &record, sizeof record);
  if (write != ESP_OK) {
    screen_after_flash();
    Serial.printf("lab NVS write failed: %s\n", esp_err_to_name(write));
    return false;
  }
  const esp_err_t committed = nvs_commit(handle);
  screen_after_flash();
  if (committed != ESP_OK) {
    Serial.printf("lab NVS commit failed: %s\n", esp_err_to_name(committed));
    return false;
  }
  Record& checked=(&record==&slot0) ? slot1 : slot0;
  if (!read_slot((next_generation & 1) ? kSlot1 : kSlot0,checked) || memcmp(&checked.data,&lab,sizeof lab)!=0) {
    Serial.println("LAB3 readback verification failed"); return false;
  }
  generation = next_generation;
  records_sync();  // TF failure must not discard the verified internal record.
  results_collect_deleted();  // Only after the user's deletion is committed.
  return true;
}

uint32_t lab_generation() { return generation; }

int lab_add_sample(const char* name, int64_t now) {
  if (!storage_ok || !name || !name[0] || lab.sample_count >= kMaxSamples) return -1;
  const int index = lab.sample_count++;
  Sample& sample = lab.samples[index];
  memset(&sample, 0, sizeof sample);
  sample.id = lab.next_id++;
  strncpy(sample.name, name, sizeof(sample.name) - 1);
  sample.created = now;
  if (lab_save()) return index;
  --lab.sample_count; --lab.next_id;
  memset(&lab.samples[lab.sample_count], 0, sizeof(Sample));
  return -1;
}

int lab_add_run(int sample_index, Run run) {
  if (!storage_ok || sample_index < 0 || sample_index >= lab.sample_count ||
      lab.run_count >= kMaxRuns || run.count < 1) return -1;
  const Sample& sample = lab.samples[sample_index];
  run.id = lab.next_id++;
  run.sample_id = sample.id;
  strncpy(run.sample, sample.name, sizeof(run.sample) - 1);
  run.sample[sizeof(run.sample) - 1] = 0;
  const int index = lab.run_count++;
  lab.runs[index] = run;
  if (lab_save()) return index;
  --lab.run_count; --lab.next_id;
  memset(&lab.runs[lab.run_count], 0, sizeof(Run));
  return -1;
}

bool lab_remove_run(int run_index) {
  if (!storage_ok || run_index < 0 || run_index >= lab.run_count) return false;
  for (int i = run_index; i < lab.run_count - 1; ++i) lab.runs[i] = lab.runs[i + 1];
  --lab.run_count;
  memset(&lab.runs[lab.run_count], 0, sizeof(Run));
  return lab_save();
}

bool lab_remove_sample(int sample_index) {
  if (!storage_ok || sample_index < 0 || sample_index >= lab.sample_count) return false;
  const uint32_t id = lab.samples[sample_index].id;
  for (int i = lab.run_count - 1; i >= 0; --i) {
    if (lab.runs[i].sample_id == id) {
      for (int j = i; j < lab.run_count - 1; ++j) lab.runs[j] = lab.runs[j + 1];
      --lab.run_count;
    }
  }
  for (int i = sample_index; i < lab.sample_count - 1; ++i) lab.samples[i] = lab.samples[i + 1];
  --lab.sample_count;
  return lab_save();
}

int lab_find_sample(uint32_t id) {
  for (int i = 0; i < lab.sample_count; ++i) if (lab.samples[i].id == id) return i;
  return -1;
}

int lab_find_run(uint32_t id) {
  for (int i = 0; i < lab.run_count; ++i) if (lab.runs[i].id == id) return i;
  return -1;
}

int lab_runs_for_sample(uint32_t sample_id) {
  int count = 0;
  for (int i = 0; i < lab.run_count; ++i) if (lab.runs[i].sample_id == sample_id) ++count;
  return count;
}
