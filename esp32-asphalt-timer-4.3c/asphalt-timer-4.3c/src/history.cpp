#include "history.hpp"
#include "report_table.hpp"
#include "result_store.hpp"
#include "tf_records.hpp"
#include "run_age.hpp"
#include <Arduino.h>
#include <SD_MMC.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <esp_random.h>

namespace {
constexpr uint32_t kMagic=0x31485341, kVersion=2;
struct Entry { Run run; uint8_t has_result; ResultRecord result; uint8_t has_comparison; uint32_t baseline; ComputedResult comparisons[kResultGroups]; };
struct ArchiveMeta { uint32_t magic, version, header_size, entry_size, count, checksum; Sample sample; uint32_t run_id; char project[24]; int64_t created; };
struct Archive { uint32_t magic, version, header_size, entry_size, count, checksum; Sample sample; uint32_t run_id; char project[24]; int64_t created; Entry entries[kMaxRuns]; };
static constexpr size_t kHeader=offsetof(Archive,entries);
static_assert(sizeof(ArchiveMeta)==kHeader,"archive header layout mismatch");
uint32_t checksum(const uint8_t* p,size_t n) { uint32_t v=2166136261u;while(n--)v=(v^*p++)*16777619u;return v; }
size_t bytes_for(uint32_t count) { return kHeader + size_t(count)*sizeof(Entry); }
String path_for(uint32_t id) {return String("/history/")+String(id)+".arc";}
bool terminated(const char* s,size_t n) {return memchr(s,0,n)!=nullptr;}
bool read_archive(const String& path, Archive* a) {
  File f=SD_MMC.open(path,FILE_READ);
  if(!f || f.size()<kHeader || f.size()>sizeof(Archive)) {f.close();return false;}
  const size_t size=f.size(); memset(a,0,sizeof(Archive));
  if(f.read(reinterpret_cast<uint8_t*>(a),size)!=size){f.close();return false;} f.close();
  if(a->magic!=kMagic||a->version!=kVersion||a->header_size!=kHeader||a->entry_size!=sizeof(Entry)||
     a->count!=1||size!=bytes_for(a->count)||!a->sample.id||!a->run_id||!terminated(a->project,sizeof a->project)||!terminated(a->sample.name,sizeof a->sample.name)) return false;
  const uint32_t expected=a->checksum;a->checksum=0;
  const uint32_t actual=checksum(reinterpret_cast<uint8_t*>(a),size);a->checksum=expected;
  if(actual!=expected)return false;
  for(unsigned i=0;i<a->count;++i) {
    const Entry& e=a->entries[i];
    if(!e.run.id||e.run.id!=a->run_id||e.run.id==a->sample.id||e.run.sample_id!=a->sample.id||e.run.count<1||e.run.count>kMaxSteps||
       unsigned(e.run.phase)>2||e.has_result>1||!terminated(e.run.name,sizeof e.run.name)||!terminated(e.run.sample,sizeof e.run.sample)||strcmp(e.run.name,a->project)||
       e.has_comparison>1||(e.has_result&&(e.result.run_id!=e.run.id||!results_archive_validate(e.result)))) return false;
    if(e.has_comparison)for(int g=0;g<kResultGroups;++g)
      if(!terminated(e.comparisons[g].text,sizeof e.comparisons[g].text)||!terminated(e.comparisons[g].unit,sizeof e.comparisons[g].unit)||!terminated(e.comparisons[g].reason,sizeof e.comparisons[g].reason))return false;
    for(int j=0;j<e.run.count;++j) if(!terminated(e.run.steps[j].name,sizeof e.run.steps[j].name)||unsigned(e.run.steps[j].mode)>2) return false;
    for(unsigned j=0;j<i;++j) if(a->entries[j].run.id==e.run.id)return false;
  }
  return true;
}
bool write_archive(const String& path,Archive* a) {
  if(!SD_MMC.exists("/history")&&!SD_MMC.mkdir("/history"))return false;
  const String tmp=path+".tmp",bak=path+".bak";
  if(SD_MMC.exists(tmp))history_recover();
  if(SD_MMC.exists(tmp))return false; // Preserve unknown or invalid interrupted data.
  const size_t size=bytes_for(a->count);a->checksum=0;a->checksum=checksum(reinterpret_cast<uint8_t*>(a),size);
  File f=SD_MMC.open(tmp,FILE_WRITE);if(!f)return false;
  const size_t written=f.write(reinterpret_cast<const uint8_t*>(a),size);f.flush();f.close();
  if(written!=size){SD_MMC.remove(tmp);return false;}
  Archive* check=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
  if(!check){SD_MMC.remove(tmp);return false;}
  const bool verified=read_archive(tmp,check) && !memcmp(a,check,size);free(check);
  if(!verified){SD_MMC.remove(tmp);return false;}
  if(SD_MMC.exists(path)) {
    if(SD_MMC.exists(bak)&&!SD_MMC.remove(bak)){SD_MMC.remove(tmp);return false;}
    if(!SD_MMC.rename(path,bak)){SD_MMC.remove(tmp);return false;}
  }
  if(!SD_MMC.rename(tmp,path)) {
    if(!SD_MMC.exists(path)&&SD_MMC.exists(bak))SD_MMC.rename(bak,path);
    return false;
  }
  return true;
}
}

bool history_run_busy(const Run& run,int64_t now) {
  for(int i=0;i<run.count;++i){const Step& s=run.steps[i];
    if(s.started&&!s.done&&!s.skipped && s.minutes>0 && (due_ms(s)>now ||
       (s.mode!=RemindMode::None&&!s.notified)))return true;}
  return false;
}
bool history_age_reached(int64_t created,int64_t now,int64_t hold,int days) {
  return created>0&&now>=created&&now>=hold&&now-created>=int64_t(days)*86400000LL;
}
int history_find_candidate(int64_t now,int days,uint32_t protected_sample,uint32_t protected_run,uint32_t only_sample) {
  int best=-1;int64_t oldest=INT64_MAX;
  for(int i=0;i<lab.run_count;++i) {
    const Run& run=lab.runs[i];const int64_t created=run_age_get(run.id);
    if((only_sample&&run.sample_id!=only_sample)||run.id==protected_run||
       (protected_sample==run.sample_id&&lab_runs_for_sample(protected_sample)<=1)||
       !history_age_reached(created,now,run_age_hold_until(run.id),days)||history_run_busy(run,now)||results_has_dependents(run.id))continue;
    if(created<oldest){oldest=created;best=i;}
  }
  return best;
}

void history_recover() {
  if(!records_ok()||!SD_MMC.exists("/history"))return;
  Archive* check=static_cast<Archive*>(ps_malloc(sizeof(Archive)));if(!check)return;
  for(int pass=0;pass<2;++pass) {
    File dir=SD_MMC.open("/history");if(!dir||!dir.isDirectory()){dir.close();break;}
    File f=dir.openNextFile();
    while(f) {
      String name=f.name();int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
      const bool candidate=!f.isDirectory()&&name.endsWith(pass==0?".arc.tmp":".arc.bak");
      f.close();
      if(candidate) {
        String digits=name.substring(0,name.length()-8);uint64_t id=0;bool valid_name=digits.length()>0&&digits.length()<=10;
        for(size_t i=0;valid_name&&i<digits.length();++i){if(digits[i]<'0'||digits[i]>'9')valid_name=false;else id=id*10+(digits[i]-'0');}
        if(valid_name&&id>0&&id<=0xffffffffULL) {
          const String final=path_for(uint32_t(id)),source=String("/history/")+name;
          if(read_archive(source,check)&&check->run_id==id) {
            if(!SD_MMC.exists(final)) {
              if(SD_MMC.rename(source,final))Serial.printf("HISTORY recovered id=%u\n",unsigned(id));
            } else if(pass==0 && read_archive(final,check)&&check->run_id==id) {
              String held=final+".held-"+String(esp_random(),HEX);
              if(!SD_MMC.exists(held))SD_MMC.rename(source,held); // Preserve both verified versions.
            }
          }
        }
      }
      f=dir.openNextFile();yield();
    }
    dir.close();
  }
  free(check);
}

int history_list(int offset,HistoryItem* items,int capacity,int date_key) {
  if(!records_ok())return -1;
  history_recover();
  File dir=SD_MMC.open("/history");if(!dir)return 0;
  if(!dir.isDirectory()){dir.close();return -1;}
  int total=0;File f=dir.openNextFile();
  while(f) {
    String name=f.name();
    if(!f.isDirectory()&&name.endsWith(".arc")) {
      ArchiveMeta header{};
      if(f.size()>=kHeader && f.read(reinterpret_cast<uint8_t*>(&header),kHeader)==kHeader &&
         header.magic==kMagic&&header.version==kVersion&&header.header_size==kHeader&&header.entry_size==sizeof(Entry)&&
         header.count==1&&f.size()==bytes_for(header.count)&&header.sample.id&&header.run_id&&lab_find_run(header.run_id)<0&&terminated(header.project,sizeof header.project)&&terminated(header.sample.name,sizeof header.sample.name)) {
        int key=0;
        if(header.created>0) {
          time_t seconds=header.created/1000;struct tm date{};localtime_r(&seconds,&date);
          key=(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;
        }
        if(date_key && key!=date_key){f.close();f=dir.openNextFile();continue;}
        if(total>=offset&&total<offset+capacity) {
          HistoryItem& item=items[total-offset];item.id=header.run_id;memcpy(item.sample,header.sample.name,sizeof item.sample);
          memcpy(item.project,header.project,sizeof item.project);item.created=header.created;
        }
        ++total;
      }
    }
    f.close();f=dir.openNextFile();
  }
  dir.close();return total;
}

const char* history_archive_run(int index,int64_t now,bool keep_sample) {
  if(!records_ok()||!results_archive_safe())return "TF卡或数据不可用";
  if(index<0||index>=lab.run_count)return "项目不存在";
  const Run run=lab.runs[index];const int sample_index=lab_find_sample(run.sample_id);
  if(sample_index<0)return "样品不存在";
  if(results_has_dependents(run.id))return "对照项目仍在使用";
  const Sample sample=lab.samples[sample_index];
  if(history_run_busy(run,now))return "项目仍在计时";
  Archive* a=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
  LabData* backup=static_cast<LabData*>(ps_malloc(sizeof(LabData)));
  if(!a||!backup){free(a);free(backup);return "内存不足";}
  memset(a,0,sizeof(Archive));*backup=lab;
  a->magic=kMagic;a->version=kVersion;a->header_size=kHeader;a->entry_size=sizeof(Entry);a->sample=sample;
  a->run_id=run.id;strncpy(a->project,run.name,sizeof a->project-1);a->created=run_age_get(run.id);
  Entry& e=a->entries[a->count++];e.run=run;e.has_result=results_archive_copy(run.id,e.result)?1:0;
  if(e.has_result&&run.phase!=AgingPhase::Original&&e.result.metric!=Metric::Mass&&e.result.metric!=Metric::Other) {
    e.has_comparison=1;
    for(int g=0;g<e.result.group_count;++g)e.comparisons[g]=result_derived(run,g,&e.baseline);
  }
  const String path=path_for(run.id);
  history_recover();
  if(SD_MMC.exists(path)) {
    Archive* old=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
    if(!old){free(a);free(backup);return "内存不足";}
    const bool compatible=read_archive(path,old)&&old->run_id==run.id&&old->sample.id==sample.id&&old->created==a->created;
    free(old);
    if(!compatible){free(a);free(backup);return "归档编号冲突";}
  }
  if(!write_archive(path,a)){free(a);free(backup);return "归档写入校验失败";}
  free(a);
  for(int i=index;i<lab.run_count-1;++i)lab.runs[i]=lab.runs[i+1];
  --lab.run_count;memset(&lab.runs[lab.run_count],0,sizeof(Run));
  if(!keep_sample&&!lab_runs_for_sample(sample.id)) {
    for(int i=sample_index;i<lab.sample_count-1;++i)lab.samples[i]=lab.samples[i+1];
    --lab.sample_count;memset(&lab.samples[lab.sample_count],0,sizeof(Sample));
  }
  if(!lab_save()){lab=*backup;free(backup);return "归档已保存，板内移除失败";}
  free(backup);return nullptr;
}

const char* history_restore(uint32_t id,int64_t now) {
  if(!records_ok()||!results_archive_safe())return "TF卡或数据不可用";
  if(now<=0)return "请先设置时钟";
  Archive* a=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
  if(!a)return "内存不足";
  if(!read_archive(path_for(id),a)||a->run_id!=id){free(a);return "归档损坏或不兼容";}
  if(lab_find_run(id)>=0||lab_find_sample(id)>=0){free(a);return "项目已在板内";}
  const int existing_sample=lab_find_sample(a->sample.id);
  if(existing_sample>=0 && (strcmp(lab.samples[existing_sample].name,a->sample.name)||lab.samples[existing_sample].created!=a->sample.created)){free(a);return "样品编号冲突";}
  if(lab_find_run(a->sample.id)>=0 || (existing_sample<0&&lab.sample_count>=kMaxSamples) || lab.run_count+a->count>kMaxRuns){free(a);return "板内项目空间不足";}
  uint32_t next=lab.next_id;
  if(a->sample.id>=next){if(a->sample.id==UINT32_MAX){free(a);return "编号无效";}next=a->sample.id+1;}
  if(id>=next) {if(id==UINT32_MAX){free(a);return "编号无效";} next=id+1;}
  for(unsigned i=0;i<a->count;++i) {
    const uint32_t run_id=a->entries[i].run.id;
    if(lab_find_run(run_id)>=0||lab_find_sample(run_id)>=0||run_id==UINT32_MAX){free(a);return "项目编号冲突";}
    if(run_id>=next)next=run_id+1;
  }
  for(unsigned i=0;i<a->count;++i) if(a->entries[i].has_result && !results_stage_import(a->entries[i].result)) {
    free(a);return "读数恢复失败，归档保留";
  }
  if(!run_age_set(id,a->created,now>0?now+86400000LL:0)){free(a);return "项目日期保存失败";}
  if(existing_sample<0)lab.samples[lab.sample_count++]=a->sample;
  for(unsigned i=0;i<a->count;++i)lab.runs[lab.run_count++]=a->entries[i].run;
  lab.next_id=next;free(a);
  if(!lab_save())return "恢复未保存，归档保留";
  return nullptr;
}

bool history_read(uint32_t id,Sample& sample,Run& run,ResultRecord& result,bool& has_result,int64_t* created,ComputedResult* comparisons,uint32_t* baseline,bool* has_comparison) {
  if(!records_ok())return false;
  Archive* a=static_cast<Archive*>(ps_malloc(sizeof(Archive)));if(!a)return false;
  const bool ok=read_archive(path_for(id),a)&&a->run_id==id;
  if(ok){sample=a->sample;run=a->entries[0].run;has_result=a->entries[0].has_result;result=a->entries[0].result;if(created)*created=a->created;
    if(comparisons)memcpy(comparisons,a->entries[0].comparisons,sizeof a->entries[0].comparisons);
    if(baseline)*baseline=a->entries[0].baseline;if(has_comparison)*has_comparison=a->entries[0].has_comparison;}
  free(a);return ok;
}

bool history_report(uint32_t id,String& path) {
  Sample sample{};Run run{};ResultRecord result{};bool has_result=false,has_comparison=false;int64_t created=0;uint32_t baseline=0;
  ComputedResult comparisons[kResultGroups]{};
  if(!history_read(id,sample,run,result,has_result,&created,comparisons,&baseline,&has_comparison))return false;
  String out="# 沥青实验数据\n\n## 样品："+report_cell(sample.name)+"\n\n"+report_columns();
  if(has_result)out+=report_rows(sample.name,run,result);
  else out+="| "+report_cell(sample.name)+" | "+report_cell(run.name)+" |  | 未完成 | 未保存读数 |  |  | 未判定 |\n";
  char date[32];record_time(created,date,sizeof date);if(created>0)date[10]=0;
  out+="\n日期："+String(created>0?date:"未记录")+" · 样品数量：1\n";
  if(has_comparison){out+="\n### 老化前后计算结果\n\n| 项目 | 结果 | 算式 |\n| --- | ---: | --- |\n";
    for(int g=0;g<result.group_count;++g){const auto& value=comparisons[g];
      out+="| "+report_name(run,result,g)+" · "+report_cell(sample.name)+" · "+report_comparison_name(result)+" | "+(value.valid?String(value.text)+" "+value.unit:String("未计算"))+" | "+(value.valid?String("归档计算快照；对照项目 ")+String(baseline)+"（未保存原算式）":report_cell(value.reason))+" |\n";}}
  if(has_result&&result.notes[0])out+="\n备注："+report_cell(result.notes)+"\n";
  path=String("/exports/history-")+String(id)+".md";return records_write_file(path,out);
}

bool history_selftest() {
  if(!records_ok())return false;
  Archive* a=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
  Archive* b=static_cast<Archive*>(ps_malloc(sizeof(Archive)));
  if(!a||!b){free(a);free(b);return false;}
  memset(a,0,sizeof(Archive));
  a->magic=kMagic;a->version=kVersion;a->header_size=kHeader;a->entry_size=sizeof(Entry);a->count=1;
  a->run_id=0xf0000001U|(esp_random()&0x0ffffffeU);a->sample.id=a->run_id-1;
  strcpy(a->sample.name,"测试样品");a->sample.created=1793590859891LL;
  strcpy(a->project,"针入度");a->created=1793590859891LL;
  Entry& e=a->entries[0];e.run.id=a->run_id;e.run.sample_id=a->sample.id;strcpy(e.run.sample,a->sample.name);
  strcpy(e.run.name,a->project);e.run.count=1;step_clear(&e.run.steps[0],"水浴",1,RemindMode::Alarm);
  e.run.steps[0].started=1793590859891LL;e.run.steps[0].done=1793590919891LL;
  result_defaults(e.result,e.run);e.has_result=1;
  const String path=path_for(a->run_id);
  const String bad=path+".bad";
  if(lab_find_run(a->run_id)>=0||lab_find_sample(a->sample.id)>=0||SD_MMC.exists(path)||SD_MMC.exists(path+".tmp")||SD_MMC.exists(path+".bak")||SD_MMC.exists(bad)){free(a);free(b);return false;}
  bool ok=write_archive(path,a)&&read_archive(path,b)&&!memcmp(a,b,bytes_for(1));
  if(ok){ok=SD_MMC.rename(path,path+".tmp");history_recover();ok=ok&&read_archive(path,b)&&!memcmp(a,b,bytes_for(1));}
  if(ok){ok=SD_MMC.rename(path,path+".bak");history_recover();ok=ok&&read_archive(path,b)&&!memcmp(a,b,bytes_for(1));}
  if(ok) {
    a->checksum^=1;File f=SD_MMC.open(bad,FILE_WRITE);
    if(!f)ok=false;else {const size_t n=f.write(reinterpret_cast<const uint8_t*>(a),bytes_for(1));f.flush();f.close();
      ok=n==bytes_for(1)&&!read_archive(bad,b);}
  }
  SD_MMC.remove(path);SD_MMC.remove(path+".tmp");SD_MMC.remove(path+".bak");SD_MMC.remove(bad);
  free(a);free(b);return ok;
}

const char* history_e2e_test(int64_t now,uint32_t* fixture_id) {
  if(!records_ok()||!results_archive_safe()||now<=0)return "precondition";
  if(lab.sample_count>=kMaxSamples||lab.run_count+2>kMaxRuns||lab.next_id>=UINT32_MAX-3)return "capacity";
  for(int i=0;i<lab.run_count;++i)if(history_run_busy(lab.runs[i],now))return "active_timer";
  LabData* before=static_cast<LabData*>(ps_malloc(sizeof(LabData)));if(!before)return "memory";*before=lab;
  const uint32_t sample_id=lab.next_id,run_id=lab.next_id+1,aged_id=lab.next_id+2;
  char date[32];record_time(now,date,sizeof date);date[10]=0;
  const String dir=String("/records/")+date+"/sample-"+String(sample_id);
  const String archive=path_for(run_id),aged_archive=path_for(aged_id),report=String("/exports/history-")+String(run_id)+".md";
  const String aged_report=String("/exports/history-")+String(aged_id)+".md";
  if(SD_MMC.exists(dir)||SD_MMC.exists(archive)||SD_MMC.exists(archive+".tmp")||SD_MMC.exists(archive+".bak")||SD_MMC.exists(report)||
     SD_MMC.exists(aged_archive)||SD_MMC.exists(aged_archive+".tmp")||SD_MMC.exists(aged_archive+".bak")||SD_MMC.exists(aged_report)) {free(before);return "path_exists";}
  int sample_index=lab_add_sample("归档自检",now);
  if(sample_index<0){free(before);return "sample_create";}
  Run original{};strcpy(original.name,"自检针入度");original.count=1;step_clear(&original.steps[0],"水浴",1,RemindMode::Alarm);
  int run_index=lab_add_run(sample_index,original,now-8LL*86400000LL);
  if(run_index<0){free(before);return "project_create";}
  original=lab.runs[run_index];const Sample original_sample=lab.samples[sample_index];
  const ResultRecord* initial=results_for(original);
  if(!initial){free(before);return "result_slot";}
  ResultRecord candidate=*initial;candidate.groups[0].count=3;
  strcpy(candidate.groups[0].readings[0],"10");strcpy(candidate.groups[0].readings[1],"12");strcpy(candidate.groups[0].readings[2],"14");
  if(!results_save(candidate)){free(before);return "result_save";}
  ResultRecord saved=*results_for(original);
  Run aged=original;aged.phase=AgingPhase::Rtfot;
  int aged_index=lab_add_run(sample_index,aged,now-8LL*86400000LL);
  if(aged_index<0){free(before);return "aged_create";}
  aged=lab.runs[aged_index];const ResultRecord* aged_initial=results_for(aged);
  if(!aged_initial){free(before);return "aged_slot";}
  ResultRecord aged_values=*aged_initial;
  strcpy(aged_values.groups[0].readings[0],"6");strcpy(aged_values.groups[0].readings[1],"6");strcpy(aged_values.groups[0].readings[2],"6");
  if(!results_save(aged_values)){free(before);return "aged_save";}
  const char* guard=history_archive_run(run_index,now);
  if(!results_has_dependents(run_id)||!guard||strcmp(guard,"对照项目仍在使用")){free(before);return "baseline_guard";}
  if(!run_age_set(aged_id,now-4LL*86400000LL)||history_find_candidate(now,7,0,0,sample_id)!=-1||
     history_find_candidate(now,3,0,0,sample_id)!=aged_index||!run_age_set(aged_id,now-8LL*86400000LL)||
     history_find_candidate(now,7,0,0,sample_id)!=aged_index){free(before);return "auto_candidate";}
  const char* error=history_archive_run(aged_index,now);
  if(error||lab_find_run(aged_id)>=0){free(before);return "aged_archive";}
  ComputedResult saved_comparison[kResultGroups]{};uint32_t baseline=0;bool has_comparison=false;
  Sample comparison_sample{};Run comparison_run{};ResultRecord comparison_result{};bool comparison_has_result=false;
  if(!history_read(aged_id,comparison_sample,comparison_run,comparison_result,comparison_has_result,nullptr,saved_comparison,&baseline,&has_comparison)||
     !has_comparison||!saved_comparison[0].valid||strcmp(saved_comparison[0].text,"50.00")||baseline!=run_id){free(before);return "comparison_snapshot";}
  if(history_find_candidate(now,7,0,0,sample_id)!=run_index||history_find_candidate(now,3,sample_id,0,sample_id)!=-1){free(before);return "sample_guard";}
  error=history_archive_run(run_index,now);
  if(error||lab_find_run(run_id)>=0||lab_find_sample(sample_id)>=0){free(before);return "archive";}
  String output;
  if(!history_report(aged_id,output)||output!=aged_report){free(before);return "aged_report";}
  File f=SD_MMC.open(aged_report,FILE_READ);if(!f){free(before);return "aged_report_open";}
  String text=f.readString();f.close();if(text.indexOf("50.00")<0){free(before);return "aged_comparison";}
  Sample read_sample{};Run read_run{};ResultRecord read_result{};bool has_result=false;int64_t created=0;
  if(!history_read(run_id,read_sample,read_run,read_result,has_result,&created)||!has_result||
     memcmp(&original_sample,&read_sample,sizeof(Sample))||memcmp(&original,&read_run,sizeof(Run))||
     memcmp(&saved,&read_result,sizeof(ResultRecord))||created!=now-8LL*86400000LL){free(before);return "readback";}
  if(!results_stage_import(read_result)){free(before);return "stage";} // Simulates interruption after the first restore phase.
  error=history_restore(run_id,now);
  if(error){free(before);return "restore";}
  const int restored=lab_find_run(run_id),restored_sample=lab_find_sample(sample_id);
  if(restored<0||restored_sample<0||memcmp(&lab.runs[restored],&original,sizeof(Run))||
     memcmp(&lab.samples[restored_sample],&original_sample,sizeof(Sample))||!results_for(lab.runs[restored])||
     memcmp(results_for(lab.runs[restored]),&saved,sizeof(ResultRecord))||run_age_get(run_id)!=created||run_age_hold_until(run_id)!=now+86400000LL){free(before);return "restore_compare";}
  if(!history_report(run_id,output)||output!=report){free(before);return "report";}
  f=SD_MMC.open(report,FILE_READ);if(!f){free(before);return "report_open";}
  text=f.readString();f.close();
  if(text.indexOf("归档自检")<0||text.indexOf("针入度")<0||text.indexOf("10、12、14")<0){free(before);return "report_content";}
  if(!lab_remove_sample(restored_sample)){free(before);return "cleanup_nvs";}
  if(lab.sample_count!=before->sample_count||lab.run_count!=before->run_count||
     memcmp(lab.samples,before->samples,sizeof(Sample)*before->sample_count)||
     memcmp(lab.runs,before->runs,sizeof(Run)*before->run_count)){free(before);return "other_data_changed";}
  free(before);
  // These paths were proven not to exist before this test and use only the test IDs.
  const String names[]={dir+"/sample.csv",dir+"/run-"+String(run_id)+".csv",dir+"/results-"+String(run_id)+".csv",dir+"/results-"+String(run_id)+".json",dir+"/run-"+String(aged_id)+".csv",dir+"/results-"+String(aged_id)+".csv",dir+"/results-"+String(aged_id)+".json"};
  for(const String& name:names){SD_MMC.remove(name);SD_MMC.remove(name+".bak");SD_MMC.remove(name+".tmp");}
  SD_MMC.rmdir(dir);SD_MMC.remove(archive);SD_MMC.remove(archive+".bak");SD_MMC.remove(archive+".tmp");SD_MMC.remove(report);
  if(fixture_id)*fixture_id=aged_id;
  else {SD_MMC.remove(aged_archive);SD_MMC.remove(aged_archive+".bak");SD_MMC.remove(aged_archive+".tmp");SD_MMC.remove(aged_report);}
  return nullptr;
}

bool history_cleanup_fixture(uint32_t id) {
  Archive* a=static_cast<Archive*>(ps_malloc(sizeof(Archive)));if(!a)return false;
  const String path=path_for(id);
  const bool fixture=read_archive(path,a)&&a->run_id==id&&a->sample.id+2==id&&
    !strcmp(a->sample.name,"归档自检")&&!strcmp(a->project,"自检针入度")&&
    a->entries[0].run.phase==AgingPhase::Rtfot&&lab_find_run(id)<0;
  free(a);if(!fixture)return false;
  if(!SD_MMC.remove(path))return false;
  SD_MMC.remove(path+".bak");SD_MMC.remove(path+".tmp");
  const String report=String("/exports/history-")+String(id)+".md";
  SD_MMC.remove(report);SD_MMC.remove(report+".bak");SD_MMC.remove(report+".tmp");
  return true;
}

const char* history_cleanup_aborted(uint32_t sample_id) {
  int index=lab_find_sample(sample_id);
  if(index<0||strcmp(lab.samples[index].name,"归档自检")||lab_runs_for_sample(sample_id))return "not_empty_test_sample";
  char date[32];record_time(lab.samples[index].created,date,sizeof date);date[10]=0;
  const String dir=String("/records/")+date+"/sample-"+String(sample_id);
  if(!lab_remove_sample(index))return "save";
  const String file=dir+"/sample.csv";SD_MMC.remove(file);SD_MMC.remove(file+".bak");SD_MMC.remove(file+".tmp");SD_MMC.rmdir(dir);
  return nullptr;
}
