#include "tf_records.hpp"
#include "lab_store.hpp"
#include "result_store.hpp"
#include <Arduino.h>
#include <SD_MMC.h>
#include <time.h>

namespace {
bool enabled=false, healthy=false;
uint32_t synced=0;
uint32_t sample_hash[kMaxSamples]={}, run_hash[kMaxRuns]={};
uint32_t result_hash[kMaxRuns]={};
uint32_t sample_ids[kMaxSamples]={}, run_ids[kMaxRuns]={};
uint32_t hash_text(const String& text) {
  uint32_t hash=2166136261u;
  for(size_t i=0;i<text.length();++i) hash=(hash ^ uint8_t(text[i]))*16777619u;
  return hash;
}
String cell(const char* value) {
  String result="\"";
  // Keep names entered by the user as text when opened in a spreadsheet.
  if(*value=='=' || *value=='+' || *value=='-' || *value=='@' || *value=='\t' || *value=='\r') result+='\'';
  for(;*value;++value) { if(*value=='\"') result+='\"'; result+=*value; }
  result+='\"'; return result;
}
String timestamp(int64_t ms) {
  if(ms<=0) return "";
  char text[32]; record_time(ms,text,sizeof text); return text;
}
String number(int64_t value) { char text[32]; snprintf(text,sizeof text,"%lld",(long long)value); return text; }
String directory(const Sample& sample) {
  char date[32]; record_time(sample.created,date,sizeof date);
  if(sample.created>0) date[10]=0;
  else strcpy(date,"undated");
  return String("/records/")+date+"/sample-"+String(sample.id);
}
bool mkdirs(const String& dir) {
  for(size_t i=1;i<=dir.length();++i) {
    if(i!=dir.length() && dir[i]!='/') continue;
    String part=dir.substring(0,i);
    if(!SD_MMC.exists(part) && !SD_MMC.mkdir(part)) return false;
  }
  return true;
}
bool write_verified(const String& path,const String& content) {
  const String tmp=path+".tmp", bak=path+".bak";
  if(SD_MMC.exists(tmp) && !SD_MMC.remove(tmp)) return false;
  File file=SD_MMC.open(tmp,FILE_WRITE);
  if(!file) return false;
  size_t count=file.write((const uint8_t*)content.c_str(),content.length());
  file.flush(); file.close();
  if(count!=content.length()) return false;
  file=SD_MMC.open(tmp,FILE_READ);
  if(!file || file.size()!=content.length()) {file.close();return false;}
  uint8_t bytes[256]; size_t position=0;
  while(position<content.length()) {
    size_t n=file.read(bytes,min(sizeof bytes,content.length()-position));
    if(!n || memcmp(bytes,content.c_str()+position,n)) {file.close();return false;}
    position+=n;
  }
  file.close();
  // Keep a previous verified version through the replacement window.
  if(SD_MMC.exists(path)) {
    if(SD_MMC.exists(bak) && !SD_MMC.remove(bak)) return false;
    if(!SD_MMC.rename(path,bak)) return false;
  }
  if(!SD_MMC.rename(tmp,path)) return false;
  return true;
}
String sample_csv(const Sample& sample) {
  return String("\xef\xbb\xbf样品ID,样品名称,建立时间(北京时间)\r\n")+
    String(sample.id)+","+cell(sample.name)+","+timestamp(sample.created)+"\r\n";
}
String run_csv(const Sample& sample,const Run& run) {
  String csv="\xef\xbb\xbf样品ID,样品名称,样品建立时间(北京时间),项目ID,老化阶段,试验类型,步骤序号,步骤名称,计划分钟,提醒方式,状态,开始时间(北京时间),计划到点(北京时间),确认完成时间(北京时间),跳过时间(北京时间),实际用时秒\r\n";
  for(int i=0;i<run.count;++i) {
    const Step& step=run.steps[i];
    const char* state=step.done?"已完成":step.skipped?"已跳过":step.started?"进行中":"待开始";
    int64_t ended=step.done?step.done:step.skipped;
    csv+=String(sample.id)+","+cell(sample.name)+","+timestamp(sample.created)+","+String(run.id)+","+
      cell(phase_label(run.phase))+","+cell(run.name)+","+String(i+1)+","+cell(step.name)+","+
      String(step.minutes)+","+cell(mode_label(step.mode))+","+cell(state)+","+timestamp(step.started)+","+
      timestamp(step.started && step.minutes>0?due_ms(step):0)+","+timestamp(step.done)+","+timestamp(step.skipped)+",";
    if(step.started>0 && ended>=step.started) csv+=number((ended-step.started)/1000);
    csv+="\r\n";
  }
  return csv;
}
}

void record_time(int64_t ms,char* out,size_t size) {
  if(ms<=0) {snprintf(out,size,"--");return;}
  const time_t seconds=ms/1000; struct tm parts{};
  localtime_r(&seconds,&parts);
  strftime(out,size,"%Y-%m-%d %H:%M:%S",&parts);
}
void records_begin(bool mounted) { enabled=mounted; records_force(); records_sync(); }
void records_force() {memset(sample_hash,0,sizeof sample_hash);memset(run_hash,0,sizeof run_hash);memset(result_hash,0,sizeof result_hash);healthy=false;}
bool records_write_file(const String& path,const String& content) {
  if(!enabled)return false;
  int slash=path.lastIndexOf('/');
  if(slash<1||!mkdirs(path.substring(0,slash)))return false;
  return write_verified(path,content);
}
bool records_ok() {return healthy;}
uint32_t records_generation() {return synced;}
bool records_sync() {
  if(!enabled) return false;
  bool ok=true;
  for(int i=0;i<lab.sample_count;++i) {
    const Sample& sample=lab.samples[i]; String dir=directory(sample);
    if(!mkdirs(dir)) {ok=false;continue;}
    const String content=sample_csv(sample); const uint32_t hash=hash_text(content);
    if(sample_ids[i]!=sample.id || sample_hash[i]!=hash) {
      if(write_verified(dir+"/sample.csv",content)) {sample_ids[i]=sample.id;sample_hash[i]=hash;}
      else ok=false;
    }
    for(int j=0;j<lab.run_count;++j) {
      const Run& run=lab.runs[j]; if(run.sample_id!=sample.id) continue;
      const String text=run_csv(sample,run); const uint32_t rh=hash_text(text);
      if(run_ids[j]!=run.id || run_hash[j]!=rh) {
        if(write_verified(dir+"/run-"+String(run.id)+".csv",text)) {run_ids[j]=run.id;run_hash[j]=rh;}
        else ok=false;
      }
      const String csv=result_csv(run),json=result_json(run);
      if(csv.length()) {
        const uint32_t checksum=hash_text(csv+json);
        if(result_hash[j]!=checksum) {
          if(write_verified(dir+"/results-"+String(run.id)+".csv",csv) && write_verified(dir+"/results-"+String(run.id)+".json",json))result_hash[j]=checksum;
          else ok=false;
        }
      }
    }
  }
  healthy=ok; if(ok) synced=lab_generation();
  Serial.printf("records_sync=%d generation=%u\n",ok,synced);
  return ok;
}
void records_print() {
  for(int i=0;i<lab.sample_count;++i) {
    const Sample& sample=lab.samples[i]; const String dir=directory(sample);
    Serial.println(dir);
    for(int j=0;j<lab.run_count;++j) {
      if(lab.runs[j].sample_id!=sample.id) continue;
      String path=dir+"/run-"+String(lab.runs[j].id)+".csv";
      File file=SD_MMC.open(path,FILE_READ);
      Serial.printf("FILE %s bytes=%u\n",path.c_str(),file?unsigned(file.size()):0);
      if(file) {while(file.available()) Serial.write(file.read());file.close();}
    }
  }
  Serial.println("RECORDS_END");
}
bool records_selftest() {
  if(!enabled) return false;
  Run skipped{}; skipped.count=2;
  step_clear(&skipped.steps[0],"加热",1,RemindMode::Alarm);
  step_clear(&skipped.steps[1],"水浴",1,RemindMode::Alarm);
  if(start_step(skipped,0,1793590859891LL,false)!=StartResult::Ok ||
     start_step(skipped,1,1793590864891LL,true)!=StartResult::Ok ||
     skipped.steps[0].started!=1793590859891LL || skipped.steps[0].skipped!=1793590864891LL) return false;
  Sample sample{}; sample.id=999; strcpy(sample.name,"=测试,\"样品\""); sample.created=1793590859891LL;
  Run run{};run.id=1000;strcpy(run.name,"针入度");run.phase=AgingPhase::Rtfot;run.count=3;
  for(int i=0;i<3;++i) step_clear(&run.steps[i],"水浴",1,RemindMode::Alarm);
  run.steps[0].started=sample.created;run.steps[0].done=sample.created+120000;
  run.steps[1].started=sample.created+120000;run.steps[1].skipped=sample.created+125000;
  const String csv=run_csv(sample,run);
  const bool fields=csv.indexOf("\"'=测试,\"\"样品\"\"\"")>=0 && csv.indexOf("\"RTFOT老化\"")>=0 &&
    csv.indexOf(",120\r\n")>=0 && csv.indexOf(",5\r\n")>=0 && csv.indexOf("已跳过")>=0 &&
    csv.indexOf("2026-11-02 11:40:59,2026-11-02 11:41:59,2026-11-02 11:42:59")>=0;
  const String path="/records/_selftest.csv";
  if(!mkdirs("/records")) return false;
  const bool ok=fields && write_verified(path,csv);
  SD_MMC.remove(path);SD_MMC.remove(path+".bak");SD_MMC.remove(path+".tmp");
  return ok;
}
