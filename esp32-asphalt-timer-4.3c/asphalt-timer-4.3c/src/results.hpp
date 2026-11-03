#pragma once
#include "step_logic.hpp"

enum class Metric : uint8_t { Penetration, Softening, Dynamic, Rotational, Mass, Other };
enum class ViscosityUnit : uint8_t { PaS, MilliPaS, Poise };
enum class SofteningRule : uint8_t { Domestic, AustralianWater, AustralianGlycerol, AustralianUnspecified };
constexpr int kResultGroups=4, kReadings=6, kNumericSize=16;
struct ResultGroup {
  char name[24];
  char readings[kReadings][kNumericSize];
  char correction[kNumericSize];
  uint8_t count;
  bool segregation;
};
struct ResultRecord {
  uint32_t run_id, revision, baseline_id;
  int64_t modified;
  Metric metric;
  ViscosityUnit input_unit;
  SofteningRule softening_rule;
  uint8_t group_count;
  char temperature[kNumericSize];
  char standard[32];
  char custom_unit[16];
  char custom_quantum[16];
  char notes[64];
  ResultGroup groups[kResultGroups];
};
struct ComputedResult {
  bool valid;
  double value;
  char text[40];
  char unit[16];
  char reason[96];
};
Metric detect_metric(const char* name);
const char* metric_label(Metric metric);
const char* metric_standard(Metric metric,bool australian,const char* run_name);
const char* input_unit_label(const ResultRecord& record);
const char* result_rule_label(const ResultRecord& record);
void result_defaults(ResultRecord& record,const Run& run);
bool parse_decimal(const char* text,double& value,bool allow_negative=false);
bool numeric_key(char* buffer,size_t size,char key,bool negative);
double round_even(double value,double quantum);
bool result_has_data(const ResultRecord& record);
ComputedResult calculate_result(const ResultRecord& record,int group);
bool compare_conditions(const ResultRecord& aged,const ResultRecord& base);
ComputedResult calculate_comparison(const ResultRecord& aged,int group,const ResultRecord& base);
int results_math_selftest();
