#pragma once
#include <Arduino.h>
bool transfer_start(const String& path,uint32_t scope=0);
bool transfer_start_scope(uint32_t scope);
uint32_t transfer_scope_count();
String transfer_report_path();
void transfer_stop();
void transfer_tick();
bool transfer_active();
const char* transfer_ssid();
const char* transfer_password();
String transfer_address();
String transfer_wifi_qr();
uint32_t transfer_downloads();
uint32_t transfer_sent();
uint32_t transfer_size();
bool transfer_selftest();
