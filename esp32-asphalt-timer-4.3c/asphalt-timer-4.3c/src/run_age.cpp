#include "run_age.hpp"
#include "lab_store.hpp"
#include "flash_guard.hpp"
#include <nvs.h>
#include <string.h>

namespace {
struct Age {uint32_t id;int64_t created;int64_t hold_until;};
struct Book {uint32_t magic,version,checksum;Age ages[kMaxRuns];};
Book book{};nvs_handle_t handle=0;bool ready=false;
uint32_t checksum(const Book& value) {
  const uint8_t* p=reinterpret_cast<const uint8_t*>(value.ages);size_t n=sizeof value.ages;uint32_t h=2166136261u;
  while(n--)h=(h^*p++)*16777619u;return h;
}
bool valid(const Book& b){return b.magic==0x31454741&&b.version==1&&b.checksum==checksum(b);}
}
bool run_age_init() {
  ready=nvs_open_from_partition("timer_nvs","runage",NVS_READWRITE,&handle)==ESP_OK;
  if(!ready)return false;
  size_t size=sizeof book;esp_err_t err=nvs_get_blob(handle,"ages",&book,&size);
  if(err==ESP_ERR_NVS_NOT_FOUND){memset(&book,0,sizeof book);book.magic=0x31454741;book.version=1;book.checksum=checksum(book);return true;}
  if(err!=ESP_OK||size!=sizeof book||!valid(book)){ready=false;return false;}
  return true;
}
int64_t run_age_get(uint32_t id) {
  if(!ready||!id)return 0;
  for(const auto& a:book.ages)if(a.id==id)return a.created;
  return 0;
}
int64_t run_age_hold_until(uint32_t id) {
  if(!ready||!id)return 0;
  for(const auto& a:book.ages)if(a.id==id)return a.hold_until;
  return 0;
}
bool run_age_set(uint32_t id,int64_t created_ms,int64_t hold_until) {
  if(!ready||!id||created_ms<0||hold_until<0)return false;
  Book next=book;int slot=-1;
  for(int i=0;i<kMaxRuns;++i)if(next.ages[i].id==id){slot=i;break;}
  if(slot<0)for(int i=0;i<kMaxRuns;++i)if(!next.ages[i].id||lab_find_run(next.ages[i].id)<0){slot=i;break;}
  if(slot<0)return false;
  next.ages[slot].id=id;next.ages[slot].created=created_ms;next.ages[slot].hold_until=hold_until;next.checksum=checksum(next);
  screen_before_flash();esp_err_t err=nvs_set_blob(handle,"ages",&next,sizeof next);
  if(err==ESP_OK)err=nvs_commit(handle);screen_after_flash();
  if(err!=ESP_OK)return false;
  Book check{};size_t size=sizeof check;
  if(nvs_get_blob(handle,"ages",&check,&size)!=ESP_OK||size!=sizeof check||!valid(check)||memcmp(&check,&next,sizeof next))return false;
  book=check;return true;
}
