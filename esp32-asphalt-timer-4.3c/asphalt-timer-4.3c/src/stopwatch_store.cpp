#include "stopwatch_store.hpp"
#include "tf_records.hpp"
#include <SD_MMC.h>
#include <esp_random.h>
#include <time.h>
#include <string.h>

namespace {
constexpr char header[]="record,total_us,interval_us,fnv32\n";
constexpr size_t row_size=62;
uint32_t checksum(const char* p,size_t size){uint32_t h=2166136261U;while(size--)h=(h^uint8_t(*p++))*16777619U;return h;}
bool parse(const char* row,uint32_t index,uint64_t& total,uint64_t& interval){
  if(row[10]!=','||row[31]!=','||row[52]!=','||row[61]!='\n')return false;
  for(int i=0;i<52;++i)if(i!=10&&i!=31&&(row[i]<'0'||row[i]>'9'))return false;
  unsigned long number=0,hash=0;unsigned long long t=0,d=0;
  if(sscanf(row,"%10lu,%20llu,%20llu,%8lx",&number,&t,&d,&hash)!=4 ||
     number!=uint64_t(index)+1 || hash!=checksum(row,53) || d>t)return false;
  total=t;interval=d;return true;
}
}

void StopwatchStore::reset(){path_="";persisted_=0;fault_=false;error_="";}
bool StopwatchStore::create(){
  if(!SD_MMC.exists("/stopwatch")&&!SD_MMC.mkdir("/stopwatch"))return false;
  char name[72];
  for(int tries=0;tries<8;++tries){
    snprintf(name,sizeof name,"/stopwatch/%llu-%08lx.csv",static_cast<unsigned long long>(time(nullptr)),static_cast<unsigned long>(esp_random()));
    if(SD_MMC.exists(name))continue;
    File f=SD_MMC.open(name,FILE_WRITE);if(!f)return false;
    const size_t n=f.write(reinterpret_cast<const uint8_t*>(header),sizeof header-1);f.flush();f.close();
    File check=SD_MMC.open(name,FILE_READ);char actual[sizeof header]={};
    const bool ok=n==sizeof header-1&&check&&check.size()==sizeof header-1&&check.read(reinterpret_cast<uint8_t*>(actual),sizeof header-1)==sizeof header-1&&!memcmp(actual,header,sizeof header-1);
    check.close();if(!ok)return false;
    path_=name;return true;
  }
  return false;
}
bool StopwatchStore::read_record(uint32_t index,uint64_t& total,uint64_t& interval){
  const uint64_t offset=sizeof header-1+uint64_t(index)*row_size;
  if(offset+row_size>UINT32_MAX)return false;
  File f=SD_MMC.open(path_,FILE_READ);char row[row_size+1]={};
  const bool ok=f&&f.size()>=offset+row_size&&f.seek(uint32_t(offset))&&f.read(reinterpret_cast<uint8_t*>(row),row_size)==row_size;
  f.close();return ok&&parse(row,index,total,interval);
}
bool StopwatchStore::write_record(uint32_t index,uint64_t total,uint64_t interval){
  const uint64_t expected=sizeof header-1+uint64_t(index)*row_size;
  if(expected+row_size>UINT32_MAX)return false;
  char row[row_size+1];
  const int prefix=snprintf(row,sizeof row,"%010lu,%020llu,%020llu,",static_cast<unsigned long>(index+1),static_cast<unsigned long long>(total),static_cast<unsigned long long>(interval));
  if(prefix!=53)return false;
  snprintf(row+53,sizeof row-53,"%08lx\n",static_cast<unsigned long>(checksum(row,53)));
  File f=SD_MMC.open(path_,FILE_APPEND);
  if(!f||f.size()!=expected){f.close();return false;}
  const size_t written=f.write(reinterpret_cast<const uint8_t*>(row),row_size);f.flush();f.close();
  uint64_t t=0,d=0;
  File check=SD_MMC.open(path_,FILE_READ);const bool size_ok=check&&check.size()==expected+row_size;check.close();
  return written==row_size&&size_ok&&read_record(index,t,d)&&t==total&&d==interval;
}
bool StopwatchStore::append(const Stopwatch& watch,uint64_t elapsed){
  error_="";
  if(!watch.running||watch.record_count==UINT32_MAX){error_="秒表未开始或记录编号已满";return false;}
  if(fault_){error_="写卡校验失败，已有记录保留；请暂停后清零";return false;}
  if(!records_ok()){
    if(path_.length()||watch.record_count>=Stopwatch::kCacheRecords){error_="TF卡不可用，请检查卡；秒表仍继续计时";return false;}
    return true; // First 100 records may be retained in RAM until a card is ready.
  }
  if(!path_.length()&&!create()){error_="无法创建秒表记录文件，请检查TF卡";return false;}
  while(persisted_<watch.record_count){
    uint64_t total=0;
    if(!watch.cached(persisted_,total)||!write_record(persisted_,total,watch.interval_us(persisted_))){fault_=true;error_="记录补存失败，原记录保留";return false;}
    ++persisted_;
  }
  uint64_t previous=0;
  if(watch.record_count&&!watch.cached(watch.record_count-1,previous)){error_="上一条记录不可用";return false;}
  if(elapsed<previous||!write_record(watch.record_count,elapsed,elapsed-previous)){fault_=true;error_="写卡校验失败，本次未确认保存";return false;}
  ++persisted_;return true;
}
bool StopwatchStore::read(const Stopwatch& watch,uint32_t index,uint64_t& total,uint64_t& interval){
  uint64_t previous=0;
  if(watch.cached(index,total)&&(!index||watch.cached(index-1,previous))){interval=total-previous;return true;}
  return index<watch.record_count&&index<persisted_&&records_ok()&&read_record(index,total,interval);
}

bool stopwatch_store_selftest(){
  if(!records_ok())return false;
  Stopwatch watch;StopwatchStore store;watch.start(1000);bool ok=true;
  for(int i=0;i<150&&ok;++i){
    const uint64_t at=1000+uint64_t(i+1)*123456;
    ok=store.append(watch,watch.elapsed_us(at))&&watch.record(at);
  }
  for(uint32_t i=0;i<150&&ok;++i){
    uint64_t total=0,interval=0;
    ok=store.read(watch,i,total,interval)&&total==uint64_t(i+1)*123456&&interval==123456;
  }
  uint64_t total=0,interval=0;
  ok=ok&&!store.read(watch,150,total,interval);
  const String fixture=store.path();
  if(ok){ // An interrupted trailing write must block further appends, never overwrite.
    File f=SD_MMC.open(fixture,FILE_APPEND);ok=bool(f)&&f.write(uint8_t('x'))==1;f.flush();f.close();
    ok=ok&&!store.append(watch,watch.elapsed_us(20000000))&&watch.record_count==150;
  }
  const bool cleaned=fixture.length()&&SD_MMC.remove(fixture);
  return ok&&cleaned;
}
