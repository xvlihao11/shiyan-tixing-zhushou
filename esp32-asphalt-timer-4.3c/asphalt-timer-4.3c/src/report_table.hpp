#pragma once
#include "results.hpp"
#include <Arduino.h>

inline String report_cell(const char* text) {
  String out;for(;*text;++text){if(*text=='|'||*text=='\\'||*text=='`'||*text=='['||*text==']')out+='\\';out+=(*text=='\r'||*text=='\n')?' ':*text;}return out;
}
inline String report_name(const Run& run,const ResultRecord& r,int group) {
  String name=run.phase==AgingPhase::Rtfot?"RTFOT 老化后 · ":run.phase==AgingPhase::Tfot?"TFOT 老化后 · ":"";
  name+=report_cell(run.name);
  if(r.group_count>1)name+=" · "+report_cell(r.groups[group].name);
  if(r.groups[group].segregation)name+=" · 离析";
  return name;
}
inline String report_columns() {
  return "| 样品 | 实验项目 | 单位 | 结果 | 原始读数 | 温度条件 | 采用标准 | 评价 |\n| --- | --- | --- | ---: | --- | --- | --- | --- |\n";
}
inline String report_rows(const char* sample,const Run& run,const ResultRecord& r) {
  String out;
  for(int g=0;g<r.group_count;++g){const auto& group=r.groups[g];const auto value=calculate_result(r,g);
    String raw;const char* mass[]={"空容器质量：","老化前总质量（容器＋沥青）：","老化后总质量（容器＋沥青）："};
    for(int j=0;j<group.count;++j){if(j)raw+=r.metric==Metric::Mass?"；":"、";
      if(r.metric==Metric::Mass&&j<3)raw+=mass[j];else if(group.segregation)raw+=j?"下部：":"上部：";
      raw+=group.readings[j][0]?report_cell(group.readings[j]):String("未填");if(r.metric==Metric::Mass)raw+=" g";}
    if(r.metric==Metric::Mass)raw+="；ΔF："+(group.correction[0]?report_cell(group.correction):String("0"))+" %";
    String condition;
    if(r.metric==Metric::Softening)condition=r.softening_rule==SofteningRule::AustralianWater?"水浴":r.softening_rule==SofteningRule::AustralianGlycerol?"甘油浴":"";
    if(r.temperature[0]){if(condition.length())condition+="；";condition+="试验 "+report_cell(r.temperature)+" ℃";}
    out+="| "+report_cell(sample)+" | "+report_name(run,r,g)+" | "+report_cell(value.unit)+" | "+(value.valid?String(value.text):String("未完成"))+" | "+raw+" | "+condition+" | "+report_cell(r.standard)+" | 未判定 |\n";
  }
  return out;
}
inline String report_comparison_name(const ResultRecord& r){return r.metric==Metric::Penetration?"针入度比":r.metric==Metric::Softening?"软化点差":"黏度比";}
inline String report_number(double v){String s(v,6);while(s.endsWith("0"))s.remove(s.length()-1);if(s.endsWith("."))s.remove(s.length()-1);return s;}
inline String report_formula(const ResultRecord& aged,int group,const ResultRecord& base) {
  int match=-1;for(int i=0;i<base.group_count;++i)if(!strcmp(aged.groups[group].name,base.groups[i].name))match=i;
  if(match<0)return "对照组缺失";
  auto a=calculate_result(aged,group),b=calculate_result(base,match);if(!a.valid||!b.valid)return "读数未填齐";
  if(aged.metric==Metric::Dynamic){auto factor=[](ViscosityUnit u){return u==ViscosityUnit::MilliPaS?.001:u==ViscosityUnit::Poise?.1:1.;};a.value*=factor(aged.input_unit);b.value*=factor(base.input_unit);}
  return report_number(a.value)+(aged.metric==Metric::Softening?" − ":" ÷ ")+report_number(b.value)+(aged.metric==Metric::Penetration?" × 100":"");
}
