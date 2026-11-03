#pragma once
#include "results.hpp"
#include <Arduino.h>
bool results_load();
const ResultRecord* results_for(const Run& run);
bool results_save(const ResultRecord& candidate);
bool results_storage_ok();
void results_collect_deleted();
uint32_t results_free_entries();
int result_baselines(const Run& run,uint32_t* ids,int size);
bool results_has_dependents(uint32_t base_id);
const Run* result_baseline(const Run& run,char* reason,size_t size);
ComputedResult result_derived(const Run& run,int group,uint32_t* baseline=nullptr);
String result_csv(const Run& run,const ResultRecord* supplied=nullptr);
String result_json(const Run& run,const ResultRecord* supplied=nullptr);
String result_fixture_json();
String result_mobile_html(uint32_t scope);
String result_markdown(uint32_t selected,bool include_times);
bool result_export(uint32_t selected,bool include_times,String& path);
bool result_store_selftest();
bool result_persistence_probe(bool prepare);
bool results_archive_safe();
bool results_archive_copy(uint32_t run_id, ResultRecord& out);
bool results_archive_validate(const ResultRecord& value);
bool results_stage_import(const ResultRecord& value);
void results_discard_staged(const uint32_t* ids, int count);
