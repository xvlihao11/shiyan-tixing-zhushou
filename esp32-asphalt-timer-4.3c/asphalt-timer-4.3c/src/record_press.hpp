#pragma once
#include <stdint.h>

constexpr int kRecordX=20,kRecordY=262,kRecordW=760,kRecordH=112;
// Only controller-confirmed frames change the latch. An empty poll is not a lift.
struct RecordPress {
  bool held=false;
  bool observe(bool fresh,uint8_t count,int x,int y,bool enabled){
    if(!fresh)return false;
    if(!count){held=false;return false;}
    if(held)return false;
    held=true;
    return enabled&&count==1&&x>=kRecordX&&x<kRecordX+kRecordW&&y>=kRecordY&&y<kRecordY+kRecordH;
  }
};
inline bool record_press_selftest(){
  RecordPress p;
  if(!p.observe(true,1,25,270,true))return false; // One-frame quick tap.
  for(int i=0;i<10;++i)if(p.observe(false,0,0,0,true)||p.observe(true,1,100,300,true))return false;
  if(p.observe(true,0,0,0,true)||!p.observe(true,1,700,360,true))return false;
  p.observe(true,0,0,0,true);
  if(p.observe(true,1,10,270,true)||p.observe(true,1,100,300,true))return false; // Drag-in.
  p.observe(true,0,0,0,true);
  if(p.observe(true,2,100,300,true)||p.observe(true,1,100,300,true))return false;
  p.observe(true,0,0,0,true);
  if(p.observe(true,1,100,300,false)||p.observe(true,1,100,300,true))return false; // Lock/modal.
  p.observe(true,0,0,0,true);
  return p.observe(true,1,100,300,true);
}
