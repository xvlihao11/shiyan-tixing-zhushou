#include "transfer.hpp"
#include <WiFi.h>
#include <DNSServer.h>
#include <SD_MMC.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <errno.h>
#include "result_store.hpp"
#include "lab_store.hpp"

namespace {
WiFiServer server(80);WiFiClient client;File file;
DNSServer dns;
uint32_t allowed_ids[kMaxRuns]={};int allowed_count=0;
bool active=false,responding=false,download=false;
String path,request,body,header;
char ssid[32]={},password[16]={};
size_t position=0,header_position=0;uint32_t last_activity=0,client_time=0,downloads=0,sent=0,length=0;
uint32_t current_scope() {
  uint32_t mask=0;for(int i=0;i<allowed_count;++i){int index=lab_find_run(allowed_ids[i]);if(index>=0)mask|=uint32_t(1)<<index;}return mask;
}
void remember_scope(uint32_t mask) {
  allowed_count=0;for(int i=0;i<lab.run_count;++i)if(mask&(uint32_t(1)<<i))allowed_ids[allowed_count++]=lab.runs[i].id;
}
void close_client() {file.close();client.stop();request="";body="";header="";responding=false;position=header_position=0;download=false;}
int send_some(const uint8_t* data,size_t size) {
  const int sent=::send(client.fd(),data,size,MSG_DONTWAIT);
  if(sent<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return 0;
  if(sent<0){Serial.printf("HTTP send failed errno=%d\n",errno);close_client();return -1;}
  return sent;
}
void respond(bool is_download,int code=200) {
  download=is_download;
  if(download) {
    file=SD_MMC.open(path,FILE_READ);
    if(!file){download=false;code=404;}
  }
  if(!download)body=code==200?result_mobile_html(current_scope()):
    code==302?"<html><meta charset=utf-8><body>正在进入试验记录选择页。<a href='http://192.168.4.1/'>点击进入</a></body></html>":
    code==400?"<html><meta charset=utf-8><body><p>请选择本次共享范围内的试验项目。</p><a href='/'>返回选择</a></body></html>":
    code==500?"<html><meta charset=utf-8><body><p>报告生成失败，请检查板子上的TF卡。</p><a href='/'>返回选择</a></body></html>":"Not found";
  const size_t size=download?file.size():body.length();
  header=String("HTTP/1.1 ")+(code==200?"200 OK":code==302?"302 Found":code==400?"400 Bad Request":code==500?"500 Internal Server Error":"404 Not Found")+"\r\nContent-Type: "+
    (download?"text/markdown; charset=utf-8":"text/html; charset=utf-8")+"\r\nContent-Length: "+String(size)+
    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
  if(download)header+="Content-Disposition: attachment; filename=\"asphalt-records.md\"\r\n";
  if(code==302)header+="Location: http://192.168.4.1/\r\n";
  header+="\r\n";responding=true;
  Serial.printf("HTTP response=%d download=%d bytes=%u\n",code,download,unsigned(size));
}
void route_export(const String& target) {
  const uint32_t scope=current_scope();uint32_t selected=0;bool times=false,valid=true;
  const int query=target.indexOf('?');int position=query+1;
  while(position>0&&position<int(target.length())) {
    int end=target.indexOf('&',position);if(end<0)end=target.length();
    const String item=target.substring(position,end);
    if(item.startsWith("id=")) {
      String digits=item.substring(3);uint64_t value=0;
      if(!digits.length()||digits.length()>10){valid=false;break;}
      for(size_t i=0;i<digits.length();++i){if(digits[i]<'0'||digits[i]>'9'){valid=false;break;}value=value*10+(digits[i]-'0');}
      if(!valid||value>0xffffffffu){valid=false;break;}
      int index=lab_find_run(uint32_t(value));
      if(index<0||!(scope&(uint32_t(1)<<index))){valid=false;break;}
      selected|=uint32_t(1)<<index;
    }else if(item=="times=1")times=true;
    position=end+1;
  }
  if(!valid||!selected){respond(false,400);return;}
  String report;
  if(!result_export(selected,times,report)){respond(false,500);return;}
  path=report;File f=SD_MMC.open(path,FILE_READ);length=f?f.size():0;f.close();sent=0;
  Serial.printf("phone_export projects_mask=%08x times=%d bytes=%u\n",unsigned(selected),times,length);
  respond(true);
}
}
bool transfer_start(const String& report,uint32_t scope) {
  if(!scope)scope=lab.run_count==32?0xffffffffu:(uint32_t(1)<<lab.run_count)-1;
  if(!scope)return false;
  uint32_t size=0;
  if(report.length()) {
    File check=SD_MMC.open(report,FILE_READ);
    if(!check||check.isDirectory()||check.size()>100000){check.close();return false;}
    size=check.size();check.close();
  }
  if(active) {close_client();remember_scope(scope);path=report;length=size;sent=downloads=0;last_activity=millis();return true;}
  transfer_stop();remember_scope(scope);length=size;
  snprintf(ssid,sizeof ssid,"Asphalt-43C-%04X",unsigned(ESP.getEfuseMac()&0xffff));
  snprintf(password,sizeof password,"%08u",unsigned(esp_random()%100000000));
  WiFi.persistent(false);WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(ssid,password,1,false,2)){WiFi.mode(WIFI_OFF);return false;}
  dns.setTTL(0);
  if(!dns.start(53,"*",WiFi.softAPIP())){WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);return false;}
  server.begin();path=report;active=true;downloads=sent=0;last_activity=millis();
  Serial.printf("transfer_start=1 ip=%s heap=%u\n",WiFi.softAPIP().toString().c_str(),ESP.getFreeHeap());
  return true;
}
bool transfer_start_scope(uint32_t scope){return transfer_start("",scope);}
uint32_t transfer_scope_count(){uint32_t n=0,mask=current_scope();while(mask){n+=mask&1;mask>>=1;}return n;}
String transfer_report_path(){return path;}
void transfer_stop(){close_client();server.end();dns.stop();if(active)WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);active=false;}
bool transfer_active(){return active;}
const char* transfer_ssid(){return ssid;}
const char* transfer_password(){return password;}
String transfer_address(){return active?String("http://")+WiFi.softAPIP().toString()+"/":String("");}
String transfer_wifi_qr(){return String("WIFI:T:WPA;S:")+ssid+";P:"+password+";;";}
uint32_t transfer_downloads(){return downloads;}
uint32_t transfer_sent(){return sent;}
uint32_t transfer_size(){return length;}
void transfer_tick() {
  if(!active)return;
  const uint32_t now=millis();
  if(!client) {
    close_client();client=server.accept();
    if(client){client_time=now;last_activity=now;request.reserve(1024);}
    else {if(now-last_activity>=300000)transfer_stop();return;}
  }
  if(now-client_time>10000){close_client();return;}
  if(!responding) {
    int budget=256;
    while(budget--&&client.available()) {
      char c=client.read();request+=c;client_time=last_activity=now;
      if(request.length()>4096){close_client();return;}
      if(request.endsWith("\r\n\r\n")) {
        const int end=request.indexOf(' ',4);const String target=end>4?request.substring(4,end):String("");
        if(request.startsWith("GET /export?"))route_export(target);
        else if(request.startsWith("GET /download HTTP/1.")&&path.length())respond(true);
        else respond(false,request.startsWith("GET / HTTP/1.")?200:302);
        break;
      }
    }
    return;
  }
  const int fd=client.fd();if(fd<0){close_client();return;}
  fd_set writable;FD_ZERO(&writable);FD_SET(fd,&writable);timeval timeout{};
  if(select(fd+1,nullptr,&writable,nullptr,&timeout)<=0||!FD_ISSET(fd,&writable))return;
  const int capacity=1024;
  size_t count=0;
  if(header_position<header.length()) {
    int n=send_some((const uint8_t*)header.c_str()+header_position,min(size_t(capacity),header.length()-header_position));if(n<0)return;count=n;header_position+=count;
  } else if(download) {
    uint8_t buffer[1024];size_t amount=min(sizeof buffer,size_t(capacity));
    if(!file.seek(position)){close_client();return;}
    size_t n=file.read(buffer,amount);
    if(n){int wrote=send_some(buffer,n);if(wrote<0)return;count=wrote;position+=count;sent=position;}
    if(position==file.size()){++downloads;Serial.printf("download_complete bytes=%u\n",unsigned(position));close_client();}
    else if(!n){close_client();return;}
  } else {
    int n=send_some((const uint8_t*)body.c_str()+position,min(size_t(capacity),body.length()-position));if(n<0)return;count=n;position+=count;
    if(position==body.length())close_client();
  }
  if(count)client_time=last_activity=now;
}
bool transfer_selftest() {
  if(!active)return false;
  WiFiClient probe;
  if(!probe.connect(WiFi.softAPIP(),80,1000)){Serial.printf("probe local connect failed errno=%d\n",errno);return false;}
  probe.print("GET /download HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n");
  uint32_t deadline=millis()+10000;String headers;bool content=false,ok=true;size_t bytes=0;
  File expected=SD_MMC.open(path,FILE_READ);
  if(!expected){probe.stop();return false;}
  while(int32_t(deadline-millis())>0&&(probe.connected()||probe.available())) {
    transfer_tick();int budget=2048;
    while(probe.available()&&budget--) {
      int c=probe.read();
      if(!content){headers+=char(c);if(headers.endsWith("\r\n\r\n")){content=true;if(!headers.startsWith("HTTP/1.1 200 OK"))ok=false;}}
      else {if(!expected.available()||expected.read()!=c)ok=false;++bytes;}
    }
    delay(1);
  }
  ok=ok&&content&&bytes==expected.size()&&!expected.available();
  Serial.printf("probe headers=%u body=%u expected=%u\n",unsigned(headers.length()),unsigned(bytes),unsigned(expected.size()));
  expected.close();probe.stop();return ok;
}
