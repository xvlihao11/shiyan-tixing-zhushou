#pragma once
#include <stdint.h>

// Screen-coordinate gesture gate, independent of GPIO and display state.
// observe() receives confirmed controller frames only. An unlock is emitted
// on the explicit lift after a deliberate left-to-right swipe.
class PocketGuard {
 public:
  bool tracking() const { return active_; }
  void reset() { *this=PocketGuard{}; }
  bool observe(uint8_t contacts,int x,int y,uint32_t tick) {
    if (!contacts) {
      if (!active_) return false;
      const bool unlock=valid_ && points_>=4 && start_x_<=160 && last_x_>=640 &&
        last_x_-start_x_>=480 && static_cast<uint32_t>(last_-start_)>=180 &&
        static_cast<uint32_t>(tick-start_)<=2400;
      reset();
      return unlock;
    }
    if (!active_) {
      active_=true;start_=tick;start_x_=last_x_=furthest_x_=x;start_y_=y;
      valid_=contacts==1 && x>=0 && x<=160 && y>=80 && y<=400;
    }
    last_=tick;
    if (contacts!=1 || y<0 || y>=480 || x<0 || x>=800 ||
        y-start_y_>80 || start_y_-y>80 || furthest_x_-x>60 ||
        static_cast<uint32_t>(tick-start_)>2200) valid_=false;
    last_x_=x;
    if (x>furthest_x_) furthest_x_=x;
    if (points_<255) ++points_;
    return false;
  }
 private:
  bool active_=false,valid_=false;
  int start_x_=0,start_y_=0,last_x_=0,furthest_x_=0;
  uint8_t points_=0;
  uint32_t start_=0,last_=0;
};

inline bool pocket_guard_selftest() {
  PocketGuard g;
  g.observe(1,80,220,100); if(g.observe(0,0,0,300)) return false; // Tap.
  g.observe(1,80,220,400);g.observe(1,80,220,1400);if(g.observe(0,0,0,1600))return false; // Hold.
  for(int i=0;i<5;++i)g.observe(1,80+i*60,220,2000+i*80);if(g.observe(0,0,0,2500))return false; // Short swipe.
  for(int i=0;i<5;++i)g.observe(1,80+i*160,100+i*50,3000+i*80);if(g.observe(0,0,0,3500))return false; // Diagonal.
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,4000+i*80);if(!g.observe(0,0,0,4500))return false; // Valid.
  for(int i=0;i<5;++i)g.observe(i==2?2:1,80+i*160,220,5000+i*80);if(g.observe(0,0,0,5500))return false; // Multitouch.
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,6000+i*700);if(g.observe(0,0,0,9000))return false; // Too slow.
  g.observe(1,80,220,10000);g.observe(1,500,220,10100);g.observe(1,200,220,10200);g.observe(1,720,220,10300);if(g.observe(0,0,0,10500))return false; // Reversal.
  const uint32_t base=0xffffff00u;
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,base+static_cast<uint32_t>(i*80));
  if(!g.observe(0,0,0,base+500u))return false; // millis rollover.
  // Fresh-only input: the one lift frame arrives immediately after the swipe.
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,11000+i*80);
  if(!g.observe(0,0,0,11340))return false;
  // Separate contacts must not be stitched into a long swipe.
  for(int i=0;i<5;++i)g.observe(1,80+i*70,220,12000+i*80);
  if(g.observe(0,0,0,12340)||g.tracking())return false;
  for(int i=0;i<5;++i)g.observe(1,380+i*85,220,12400+i*80);
  if(g.observe(0,0,0,12740)||g.tracking())return false;
  // A rejected short contact cannot leave later valid gestures stuck.
  g.observe(1,80,220,13000);if(g.observe(0,0,0,13020)||g.tracking())return false;
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,13100+i*80);
  if(!g.observe(0,0,0,13440))return false;
  // Immediate lift remains valid across the millis rollover.
  for(int i=0;i<5;++i)g.observe(1,80+i*160,220,base+static_cast<uint32_t>(i*80));
  return g.observe(0,0,0,base+340u);
}
