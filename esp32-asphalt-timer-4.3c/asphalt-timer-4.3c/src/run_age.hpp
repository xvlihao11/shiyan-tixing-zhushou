#pragma once
#include <stdint.h>
bool run_age_init();
int64_t run_age_get(uint32_t id);
int64_t run_age_hold_until(uint32_t id);
bool run_age_set(uint32_t id,int64_t created_ms,int64_t hold_until=0);
