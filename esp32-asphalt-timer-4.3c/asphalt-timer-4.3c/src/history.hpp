#pragma once
#include <stdint.h>
#include "lab_store.hpp"
#include "results.hpp"
#include <Arduino.h>

struct HistoryItem { uint32_t id; char sample[32]; char project[24]; int64_t created; };
// Listing reports all entries and copies only the requested page.
int history_list(int offset, HistoryItem* items, int capacity, int date_key=0);
const char* history_archive_run(int run_index, int64_t now, bool keep_sample=false);
const char* history_restore(uint32_t run_id, int64_t now);
bool history_read(uint32_t run_id,Sample& sample,Run& run,ResultRecord& result,bool& has_result,int64_t* created=nullptr,ComputedResult* comparisons=nullptr,uint32_t* baseline=nullptr,bool* has_comparison=nullptr);
bool history_report(uint32_t run_id,String& path);
bool history_selftest();
void history_recover();
const char* history_e2e_test(int64_t now,uint32_t* fixture_id=nullptr);
const char* history_cleanup_aborted(uint32_t sample_id);
bool history_cleanup_fixture(uint32_t run_id);
bool history_run_busy(const Run& run,int64_t now);
bool history_age_reached(int64_t created,int64_t now,int64_t hold,int days);
int history_find_candidate(int64_t now,int days,uint32_t protected_sample=0,uint32_t protected_run=0,uint32_t only_sample=0);
