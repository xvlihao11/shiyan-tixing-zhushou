#include "result_store.hpp"
#include "lab_store.hpp"
#include "tf_records.hpp"
#include "flash_guard.hpp"
#include <nvs.h>
#include <stddef.h>
#include <time.h>
#include <esp_random.h>
#include <SD_MMC.h>

namespace {
struct Stored {uint32_t magic,version,size,checksum;ResultRecord data;};
ResultRecord cache[kMaxRuns]{};
bool blocked[kMaxRuns]={};
nvs_handle_t handle=0;bool ready=false;
uint32_t crc(const void* ptr,size_t size) {
  uint32_t value=2166136261u;const uint8_t* p=(const uint8_t*)ptr;
  while(size--)value=(value ^ *p++)*16777619u;return value;
}
bool terminated(const char* text,size_t size) {return memchr(text,0,size)!=nullptr;}
bool valid(const ResultRecord& r) {
  if(!r.run_id||r.group_count<1||r.group_count>kResultGroups||unsigned(r.metric)>5||unsigned(r.input_unit)>2||unsigned(r.softening_rule)>3 ||
    !terminated(r.temperature,sizeof r.temperature)||!terminated(r.standard,sizeof r.standard)||!terminated(r.notes,sizeof r.notes)||
    !terminated(r.custom_unit,sizeof r.custom_unit)||!terminated(r.custom_quantum,sizeof r.custom_quantum))return false;
  double v=0;if(r.temperature[0]&&!parse_decimal(r.temperature,v,true))return false;
  if(!parse_decimal(r.custom_quantum,v)||v<.000001||v>1000000)return false;
  for(int j=0;j<r.group_count;++j) {
    const auto& g=r.groups[j];
    if(g.count<1||g.count>kReadings||!terminated(g.name,sizeof g.name)||!g.name[0]||!terminated(g.correction,sizeof g.correction))return false;
    if((r.metric==Metric::Softening&&g.count!=2)||(r.metric==Metric::Mass&&g.count!=3))return false;
    if(g.correction[0]&&!parse_decimal(g.correction,v,true))return false;
    for(int i=0;i<kReadings;++i)if(!terminated(g.readings[i],kNumericSize)||(g.readings[i][0]&&!parse_decimal(g.readings[i],v)))return false;
    for(int i=0;i<j;++i)if(!strcmp(g.name,r.groups[i].name))return false;
  }
  return true;
}
size_t encode(const Stored& record,uint8_t* out) {
  memcpy(out,&record,16);const uint8_t* source=(const uint8_t*)&record.data;
  size_t input=0,output=16;
  while(input<sizeof(ResultRecord)) {
    const bool zero=source[input]==0;size_t n=1;
    while(n<128&&input+n<sizeof(ResultRecord)&&(source[input+n]==0)==zero)++n;
    if(output+1+(zero?0:n)>sizeof(Stored)){memcpy(out,&record,sizeof record);return sizeof record;}
    out[output++]=(zero?0x80:0)|uint8_t(n-1);
    if(!zero){memcpy(out+output,source+input,n);output+=n;}input+=n;
  }
  if(output>=sizeof(Stored)){memcpy(out,&record,sizeof record);return sizeof record;}
  const uint32_t version=2;memcpy(out+4,&version,4);return output;
}
bool read(int slot,Stored& record) {
  uint8_t packed[sizeof(Stored)]={};char key[8];snprintf(key,sizeof key,"r%d",slot);size_t size=sizeof packed;
  if(nvs_get_blob(handle,key,packed,&size)!=ESP_OK||size<16)return false;
  memcpy(&record,packed,16);
  if(record.magic!=0x31525341||record.size!=sizeof(ResultRecord))return false;
  if(record.version==1){if(size!=sizeof record)return false;memcpy(&record.data,packed+16,sizeof record.data);}
  else if(record.version==2) {
    uint8_t* output=(uint8_t*)&record.data;size_t input=16,position=0;
    while(input<size) {
      uint8_t control=packed[input++];size_t n=(control&127)+1;
      if(position+n>sizeof(ResultRecord))return false;
      if(control&128)memset(output+position,0,n);
      else {if(input+n>size)return false;memcpy(output+position,packed+input,n);input+=n;}
      position+=n;
    }
    if(position!=sizeof(ResultRecord))return false;
  }else return false;
  return crc(&record.data,sizeof record.data)==record.checksum&&valid(record.data);
}
String quoted(const char* text) {
  String out="\"";
  if(*text=='='||*text=='+'||*text=='-'||*text=='@')out+='\'';
  for(;*text;++text){if(*text=='\"')out+='\"';out+=*text;}
  return out+'\"';
}
String json_string(const char* text) {
  String out="\"";
  for(;*text;++text){unsigned char c=*text;if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32){char b[8];snprintf(b,sizeof b,"\\u%04x",c);out+=b;}else out+=char(c);}
  return out+'"';
}
String md(const char* text) {String out;for(;*text;++text){if(*text=='|'||*text=='\\'||*text=='`'||*text=='['||*text==']')out+='\\';out+=(*text=='\r'||*text=='\n')?' ':*text;}return out;}
String when(int64_t value) {char text[32];record_time(value,text,sizeof text);return value>0?String(text):String("--");}
bool chosen(uint32_t mask,int i) {return (mask&(uint32_t(1)<<i))!=0;}
}
bool results_load() {
  ready=nvs_open_from_partition("timer_nvs","results4c",NVS_READWRITE,&handle)==ESP_OK;
  if(!ready)return false;
  for(int i=0;i<kMaxRuns;++i) {
    Stored record{};char key[8];snprintf(key,sizeof key,"r%d",i);size_t size=0;
    if(nvs_get_blob(handle,key,nullptr,&size)==ESP_ERR_NVS_NOT_FOUND)continue;
    if(read(i,record))cache[i]=record.data;
    else {blocked[i]=true;Serial.printf("Result slot %d invalid; preserved\n",i);}
  }
  Serial.printf("results_ready=1 bytes_per_record=%u free_entries=%u\n",unsigned(sizeof(Stored)),results_free_entries());
  return true;
}
bool results_storage_ok(){return ready;}
void results_collect_deleted() {
  if(!ready)return;
  bool dirty=false;
  for(int i=0;i<kMaxRuns;++i)if(!blocked[i]&&cache[i].run_id&&lab_find_run(cache[i].run_id)<0) {
    if(!dirty)screen_before_flash();dirty=true;
    char key[8];snprintf(key,sizeof key,"r%d",i);
    esp_err_t erased=nvs_erase_key(handle,key);
    if(erased==ESP_OK||erased==ESP_ERR_NVS_NOT_FOUND)memset(&cache[i],0,sizeof cache[i]);
  }
  if(dirty){nvs_commit(handle);screen_after_flash();}
}
uint32_t results_free_entries(){nvs_stats_t stats{};return nvs_get_stats("timer_nvs",&stats)==ESP_OK?stats.free_entries:0;}
const ResultRecord* results_for(const Run& run) {
  for(int i=0;i<kMaxRuns;++i)if(!blocked[i]&&cache[i].run_id==run.id)return &cache[i];
  for(int i=0;i<kMaxRuns;++i)if(!blocked[i]&&(!cache[i].run_id||lab_find_run(cache[i].run_id)<0)) {
    result_defaults(cache[i],run);return &cache[i];
  }
  return nullptr;
}
bool results_save(const ResultRecord& candidate) {
  if(!ready||!valid(candidate)||lab_find_run(candidate.run_id)<0)return false;
  int slot=-1;
  for(int i=0;i<kMaxRuns;++i)if(!blocked[i]&&cache[i].run_id==candidate.run_id){slot=i;break;}
  if(slot<0)return false;
  Stored record{};record.magic=0x31525341;record.version=1;record.size=sizeof(ResultRecord);record.data=candidate;
  record.data.revision=cache[slot].revision+1;
  record.checksum=crc(&record.data,sizeof record.data);
  uint8_t packed[sizeof(Stored)]={};const size_t size=encode(record,packed);
  const uint32_t reserve=(sizeof(LabData)+31)/32+96;
  if(results_free_entries()<(size+31)/32+2+reserve){Serial.println("result save refused: preserving timer write reserve");return false;}
  char key[8];snprintf(key,sizeof key,"r%d",slot);
  screen_before_flash();const esp_err_t write=nvs_set_blob(handle,key,packed,size);
  const esp_err_t commit=write==ESP_OK?nvs_commit(handle):write;screen_after_flash();
  if(commit!=ESP_OK){Serial.printf("result save failed: %s\n",esp_err_to_name(commit));return false;}
  Stored checked{};
  if(!read(slot,checked)||memcmp(&checked.data,&record.data,sizeof record.data))return false;
  cache[slot]=checked.data;records_sync();return true;
}
int result_baselines(const Run& run,uint32_t* ids,int size) {
  const ResultRecord* r=results_for(run);if(!r)return 0;
  int count=0;
  for(int i=0;i<lab.run_count;++i) {
    const Run& base=lab.runs[i];if(base.sample_id!=run.sample_id||base.phase!=AgingPhase::Original||base.id==run.id)continue;
    const ResultRecord* b=results_for(base);
    if(b&&compare_conditions(*r,*b)){if(count<size)ids[count]=base.id;++count;}
  }
  return count;
}
const Run* result_baseline(const Run& run,char* reason,size_t size) {
  auto fail=[&](const char* why)->const Run*{snprintf(reason,size,"%s",why);return nullptr;};
  if(run.phase==AgingPhase::Original)return fail("未老化项目不计算老化比值");
  const ResultRecord* r=results_for(run);if(!r)return fail("读数存储不可用");
  if(r->metric==Metric::Mass||r->metric==Metric::Other)return fail("该项目无老化比值");
  uint32_t ids[kMaxRuns]={};int n=result_baselines(run,ids,kMaxRuns);
  if(!n)return fail("缺少同条件未老化项目");
  if(r->baseline_id) {
    for(int i=0;i<n;++i)if(ids[i]==r->baseline_id){int index=lab_find_run(ids[i]);return index>=0?&lab.runs[index]:nullptr;}
    return fail("已选对照条件发生变化");
  }
  if(n!=1)return fail("存在多个对照，请在参数中选择");
  return &lab.runs[lab_find_run(ids[0])];
}
ComputedResult result_derived(const Run& run,int group,uint32_t* baseline) {
  char reason[96];const Run* base=result_baseline(run,reason,sizeof reason);
  if(!base){ComputedResult out{};strcpy(out.text,"--");strcpy(out.reason,reason);return out;}
  const ResultRecord* a=results_for(run);const ResultRecord* b=results_for(*base);
  if(baseline)*baseline=base->id;
  return calculate_comparison(*a,group,*b);
}
String result_csv(const Run& run,const ResultRecord* supplied) {
  const ResultRecord* r=supplied?supplied:results_for(run);if(!r||(!r->revision&&!result_has_data(*r)))return "";
  String out="\xef\xbb\xbf样品,项目ID,老化阶段,试验,组别,模式,采用规范,温度℃,输入单位,读数1,读数2,读数3,读数4,读数5,读数6,质量修正%,结果,结果单位,对照项目ID,老化前后结果,比较单位,修改时间(北京时间)\r\n";
  for(int g=0;g<r->group_count;++g) {
    const auto& group=r->groups[g];auto result=calculate_result(*r,g);uint32_t base=0;auto derived=result_derived(run,g,&base);
    out+=quoted(run.sample)+","+String(run.id)+","+quoted(phase_label(run.phase))+","+quoted(run.name)+","+quoted(group.name)+","+
      quoted(group.segregation?"离析":"普通")+","+quoted(r->standard)+","+quoted(r->temperature)+","+quoted(input_unit_label(*r))+",";
    for(int i=0;i<kReadings;++i)out+=quoted(group.readings[i])+",";
    out+=quoted(group.correction)+","+quoted(result.text)+","+quoted(result.unit)+","+(base?String(base):String(""))+","+
      quoted(derived.text)+","+quoted(derived.unit)+","+when(r->modified)+"\r\n";
  }
  return out;
}
String result_json(const Run& run,const ResultRecord* supplied) {
  const ResultRecord* r=supplied?supplied:results_for(run);if(!r||(!r->revision&&!result_has_data(*r)))return "";
  char modified[32];snprintf(modified,sizeof modified,"%lld",(long long)r->modified);
  String out="{\"schema\":1,\"run_id\":"+String(run.id)+",\"sample_id\":"+String(run.sample_id)+",\"sample\":"+json_string(run.sample)+
    ",\"project\":"+json_string(run.name)+",\"phase\":"+String(unsigned(run.phase))+",\"revision\":"+String(r->revision)+",\"modified_ms\":"+modified+
    ",\"metric\":"+String(unsigned(r->metric))+",\"input_unit\":"+json_string(input_unit_label(*r))+",\"softening_rule\":"+String(unsigned(r->softening_rule))+
    ",\"temperature\":"+json_string(r->temperature)+",\"standard\":"+json_string(r->standard)+",\"notes\":"+json_string(r->notes)+",\"custom_unit\":"+json_string(r->custom_unit)+",\"custom_quantum\":"+json_string(r->custom_quantum)+",\"baseline_id\":"+String(r->baseline_id)+",\"groups\":[";
  for(int g=0;g<r->group_count;++g) {
    const auto& group=r->groups[g];if(g)out+=",";
    out+="{\"name\":"+json_string(group.name)+",\"segregation\":"+(group.segregation?"true":"false")+",\"mass_correction\":"+json_string(group.correction)+",\"readings\":[";
    for(int i=0;i<group.count;++i){if(i)out+=",";out+=json_string(group.readings[i]);}out+="]}";
  }
  out+="],\"steps\":[";
  for(int i=0;i<run.count;++i) {
    if(i)out+=",";const auto& step=run.steps[i];char numbers[180];
    snprintf(numbers,sizeof numbers,",\"minutes\":%d,\"mode\":%u,\"started_ms\":%lld,\"done_ms\":%lld,\"skipped_ms\":%lld}",step.minutes,unsigned(step.mode),(long long)step.started,(long long)step.done,(long long)step.skipped);
    out+="{\"name\":"+json_string(step.name)+numbers;
  }
  return out+"]}\n";
}
String result_markdown(uint32_t selected,bool times) {
  String out;out.reserve(16384);out="# 沥青试验记录\n\n时间采用北京时间；完成时间以人工确认为准。\n\n";
  for(int sample=0;sample<lab.sample_count;++sample) {
    bool header=false;
    for(int i=0;i<lab.run_count;++i) {
      const Run& run=lab.runs[i];if(!chosen(selected,i)||run.sample_id!=lab.samples[sample].id)continue;
      const ResultRecord* r=results_for(run);if(!r)continue;
      if(!header){out+="## 样品："+md(lab.samples[sample].name)+"\n\n建立："+when(lab.samples[sample].created)+"\n\n";header=true;}
      out+="### "+String(phase_label(run.phase))+" · "+md(run.name)+"（项目 "+String(run.id)+"）\n\n";
      out+="采用规范："+md(r->standard)+"；温度："+(r->temperature[0]?md(r->temperature):String("未填写"))+" ℃；输入单位："+input_unit_label(*r)+"。\n\n";
      if(r->metric==Metric::Softening)out+=String(result_rule_label(*r))+"；离析取原始上、下部读数绝对差。\n\n";
      if(r->metric==Metric::Mass)out+="质量变化 =（老化后总质量 − 老化前总质量）÷（老化前总质量 − 空容器质量）×100% ＋ 修正值；正号为增重，负号为减重。\n\n";
      out+="| 组别 | 模式 | 原始读数 | 结果 | 单位 |\n|---|---|---|---:|---|\n";
      for(int g=0;g<r->group_count;++g) {
        const auto& group=r->groups[g];auto result=calculate_result(*r,g);
        out+="| "+md(group.name)+" | "+(group.segregation?String("离析"):String("普通"))+" | ";
        const char* mass[]={"空容器","老化前总质量","老化后总质量"};
        for(int j=0;j<group.count;++j) {
          if(j)out+="；";
          if(r->metric==Metric::Mass)out+=String(mass[j])+"：";
          else if(group.segregation)out+=j?"下部：":"上部：";
          out+=group.readings[j][0]?md(group.readings[j]):String("未填");
        }
        if(r->metric==Metric::Mass)out+="；修正："+(group.correction[0]?md(group.correction):String("0"))+" %";
        out+=" | "+(result.valid?String(result.text):String("未完成"))+" | "+String(result.unit)+" |\n";
      }
      out+="\n";
      if(run.phase!=AgingPhase::Original&&r->metric!=Metric::Mass&&r->metric!=Metric::Other) {
        char reason[96];const Run* base=result_baseline(run,reason,sizeof reason);
        const int base_index=base?lab_find_run(base->id):-1;
        if(base_index>=0&&chosen(selected,base_index)) {
          for(int g=0;g<r->group_count;++g) {
            auto derived=result_derived(run,g);
            if(derived.valid)out+="- "+md(r->groups[g].name)+" · "+(r->metric==Metric::Softening?String("软化点变化（老化后−老化前）"):
              r->metric==Metric::Penetration?String("针入度比（老化后÷老化前×100%）"):String("黏度比（老化后÷老化前）"))+"：**"+derived.text+" "+derived.unit+"**，对照项目 "+String(base->id)+"。\n";
            else out+="- 老化前后计算未完成："+String(derived.reason)+"。\n";
          }
        }else out+="- 未附老化前后结果："+(base?String("未选择对照项目"):String(reason))+"。\n";
        out+="\n";
      }
      if(r->notes[0])out+="备注："+md(r->notes)+"\n\n";
      if(times) {
        out+="| 步骤 | 计划分钟 | 开始 | 计划到点 | 确认完成 | 跳过 |\n|---|---:|---|---|---|---|\n";
        for(int j=0;j<run.count;++j) {
          const auto& step=run.steps[j];out+="| "+md(step.name)+" | "+String(step.minutes)+" | "+when(step.started)+" | "+when(step.started&&step.minutes>0?due_ms(step):0)+" | "+when(step.done)+" | "+when(step.skipped)+" |\n";
        }
        out+="\n";
      }
      if(out.length()>100000)return "";
    }
  }
  return out;
}
bool result_export(uint32_t selected,bool times,String& path) {
  if(!selected)return false;
  String content=result_markdown(selected,times);if(!content.length())return false;
  char file[80];snprintf(file,sizeof file,"/exports/asphalt-%llu-%08x.md",(unsigned long long)time(nullptr),unsigned(esp_random()));
  if(!records_write_file(file,content))return false;
  path=file;return true;
}
String result_mobile_html(uint32_t scope) {
  auto html=[](const char* value)->String {
    String out;for(;*value;++value){switch(*value){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&#39;";break;default:out+=*value;}}return out;
  };
  auto ids=[&](uint32_t mask)->String {String out;for(int i=0;i<lab.run_count;++i)if(chosen(mask,i)){if(out.length())out+=",";out+=String(lab.runs[i].id);}return out;};
  String out;out.reserve(12000);
  out=R"html(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>选择导出的试验数据</title><style>body{font:16px system-ui,sans-serif;color:#173a43;background:#f4f7f8;max-width:760px;margin:auto;padding:18px 14px 110px}h1{font-size:23px}p{line-height:1.6;color:#567078}details{background:white;border:1px solid #d7e4e6;border-radius:12px;padding:10px;margin:10px 0}details details{margin-left:10px;background:#fbfdfd}summary{cursor:pointer;font-weight:650;padding:7px;line-height:1.5}input[type=checkbox]{width:22px;height:22px;margin:0 10px 0 0;vertical-align:middle;accent-color:#126a77}.run{display:flex;gap:8px;align-items:center;padding:14px 8px;border-top:1px solid #e3edef}.run span{flex:1}.run small{display:block;color:#64808a;margin-top:4px}button{border:0;border-radius:10px;padding:12px 16px;background:#e2eef0;color:#17515c;font-size:15px;margin:4px}.footer{position:fixed;bottom:0;left:0;right:0;background:#fff;padding:12px;border-top:1px solid #d5e4e6;text-align:center}.send{background:#126a77;color:white;min-width:220px}.option{display:block;padding:14px 2px}#count{margin:7px}a{color:#126a77}</style><body><h1>选择导出的试验数据</h1><p>展开日期、样品和老化阶段，勾选需要的试验项目。只导出本次勾选的内容。</p><button type="button" onclick="allRuns(true)">全选</button><button type="button" onclick="allRuns(false)">清空</button><form action="/export" method="get">)html";
  uint32_t visited=0;
  for(int sample=0;sample<lab.sample_count;++sample) {
    if(visited&(uint32_t(1)<<sample))continue;
    char date[32];record_time(lab.samples[sample].created,date,sizeof date);date[10]=0;
    if(lab.samples[sample].created<=0)strcpy(date,"日期未设置");
    uint32_t date_runs=0;
    for(int s=0;s<lab.sample_count;++s) {
      char other[32];record_time(lab.samples[s].created,other,sizeof other);other[10]=0;
      if(lab.samples[s].created<=0)strcpy(other,"日期未设置");
      if(strcmp(date,other))continue;
      for(int i=0;i<lab.run_count;++i)if(chosen(scope,i)&&lab.runs[i].sample_id==lab.samples[s].id)date_runs|=uint32_t(1)<<i;
    }
    if(!date_runs)continue;
    out+="<details open><summary><input type=checkbox data-ids='"+ids(date_runs)+"' onclick='pick(this,event)'>"+html(date)+"</summary>";
    for(int s=0;s<lab.sample_count;++s) {
      char other[32];record_time(lab.samples[s].created,other,sizeof other);other[10]=0;if(lab.samples[s].created<=0)strcpy(other,"日期未设置");
      if(strcmp(date,other))continue;visited|=uint32_t(1)<<s;
      uint32_t sample_runs=0;for(int i=0;i<lab.run_count;++i)if(chosen(date_runs,i)&&lab.runs[i].sample_id==lab.samples[s].id)sample_runs|=uint32_t(1)<<i;
      if(!sample_runs)continue;
      out+="<details open><summary><input type=checkbox data-ids='"+ids(sample_runs)+"' onclick='pick(this,event)'>"+html(lab.samples[s].name)+"</summary>";
      for(int phase=0;phase<3;++phase) {
        uint32_t phase_runs=0;for(int i=0;i<lab.run_count;++i)if(chosen(sample_runs,i)&&unsigned(lab.runs[i].phase)==unsigned(phase))phase_runs|=uint32_t(1)<<i;
        if(!phase_runs)continue;
        out+="<details open><summary><input type=checkbox data-ids='"+ids(phase_runs)+"' onclick='pick(this,event)'>"+String(phase_label(AgingPhase(phase)))+"</summary>";
        for(int i=0;i<lab.run_count;++i)if(chosen(phase_runs,i)) {
          const auto* record=results_for(lab.runs[i]);
          out+="<label class=run><input type=checkbox name=id value='"+String(lab.runs[i].id)+"' onchange='sync()'><span>"+html(lab.runs[i].name)+
            "<small>项目 "+String(lab.runs[i].id)+(record?String(" · ")+String(record->group_count)+"组 · "+html(input_unit_label(*record)):String(""))+"</small></span></label>";
        }
        out+="</details>";
      }
      out+="</details>";
    }
    out+="</details>";
  }
  out+=R"html(<label class=option><input type=checkbox name=times value=1>附带试验步骤及开始、完成时间</label><p>需要老化前后比值时，请同时勾选对应的未老化项目。不会自动增加项目。</p><div class=footer><div id=count>已选择 0 项试验</div><button class=send type=submit>导出选中项目 .md</button></div></form><script>const runs=()=>[...document.querySelectorAll('input[name=id]')];function sync(){const r=runs();document.getElementById('count').textContent='已选择 '+r.filter(x=>x.checked).length+' 项试验';document.querySelectorAll('[data-ids]').forEach(p=>{const ids=p.dataset.ids.split(',');const a=r.filter(x=>ids.includes(x.value));p.checked=a.length>0&&a.every(x=>x.checked);p.indeterminate=a.some(x=>x.checked)&&!p.checked})}function pick(p,e){e.stopPropagation();const ids=p.dataset.ids.split(',');runs().filter(x=>ids.includes(x.value)).forEach(x=>x.checked=p.checked);sync()}function allRuns(v){runs().forEach(x=>x.checked=v);sync()}document.querySelector('form').addEventListener('submit',e=>{if(!runs().some(x=>x.checked)){e.preventDefault();alert('请先勾选至少一项试验')}});sync();</script></body></html>)html";
  return out;
}
bool result_store_selftest() {
  if(!result_persistence_probe(true)||!result_persistence_probe(false))return false;
  const bool ok=records_write_file("/records/_result-selftest.json",result_fixture_json());
  SD_MMC.remove("/records/_result-selftest.json");SD_MMC.remove("/records/_result-selftest.json.bak");return ok;
}
String result_fixture_json() {
  Run run{};run.id=0xffff0001;strcpy(run.name,"RTFOT老化");strcpy(run.sample,"自检样品");
  run.count=1;step_clear(&run.steps[0],"老化",85,RemindMode::Alarm);
  run.steps[0].started=1793590859891LL;run.steps[0].done=1793595959891LL;
  ResultRecord record{};result_defaults(record,run);strcpy(record.groups[0].name,"试样,\"A\"");
  strcpy(record.groups[0].readings[0],"137.5990");strcpy(record.groups[0].readings[1],"172.5527");strcpy(record.groups[0].readings[2],"172.5769");
  return result_json(run,&record);
}
bool result_persistence_probe(bool prepare) {
  if(!ready)return false;
  Stored record{};
  if(prepare) {
    Run run{};run.id=0xffff0001;strcpy(run.name,"RTFOT老化");result_defaults(record.data,run);
    strcpy(record.data.groups[0].readings[0],"137.5990");strcpy(record.data.groups[0].readings[1],"172.5527");strcpy(record.data.groups[0].readings[2],"172.5769");
    record.magic=0x31525341;record.version=1;record.size=sizeof(ResultRecord);record.checksum=crc(&record.data,sizeof record.data);
    uint8_t packed[sizeof(Stored)]={};size_t size=encode(record,packed);
    screen_before_flash();esp_err_t saved=nvs_set_blob(handle,"r99",packed,size);
    if(saved==ESP_OK)saved=nvs_commit(handle);screen_after_flash();return saved==ESP_OK;
  }
  bool ok=read(99,record)&&record.data.run_id==0xffff0001&&
    !strcmp(record.data.groups[0].readings[1],"172.5527")&&!strcmp(calculate_result(record.data,0).text,"+0.069");
  screen_before_flash();nvs_erase_key(handle,"r99");nvs_commit(handle);screen_after_flash();return ok;
}
