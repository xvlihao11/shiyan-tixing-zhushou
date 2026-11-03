#include "results.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Metric detect_metric(const char* name) {
  if(strstr(name,"针入度"))return Metric::Penetration;
  if(strstr(name,"软化点"))return Metric::Softening;
  if(strstr(name,"动力黏度")||strstr(name,"动力粘度"))return Metric::Dynamic;
  if(strstr(name,"旋转黏度")||strstr(name,"旋转粘度"))return Metric::Rotational;
  if(strstr(name,"RTFOT")||strstr(name,"TFOT")||strstr(name,"质量"))return Metric::Mass;
  return Metric::Other;
}
const char* metric_label(Metric metric) {
  const char* labels[]={"针入度","软化点","动力黏度","旋转黏度","质量变化","其他数值"};
  return labels[static_cast<unsigned>(metric)<6?static_cast<unsigned>(metric):5];
}
const char* metric_standard(Metric metric,bool australian,const char* name) {
  if(metric==Metric::Penetration)return australian?"AS 2341.12:2020":"JTG 3410—2025 / T 0604";
  if(metric==Metric::Softening)return australian?"AS 2341.18:2020":"JTG 3410—2025 / T 0606—2011";
  if(metric==Metric::Dynamic)return australian?"AS/NZS 2341.2":"JTG 3410—2025 / T 0620";
  if(metric==Metric::Rotational)return australian?"AS/NZS 2341.4":"JTG 3410—2025 / T 0625";
  if(metric==Metric::Mass) {
    const bool rtfot=strstr(name,"RTFOT")!=nullptr;
    const bool tfot=strstr(name,"TFOT")!=nullptr;
    return australian?(rtfot?"AS/NZS 2341.10":""):rtfot?"JTG 3410—2025 / T 0610":tfot?"JTG 3410—2025 / T 0609":"自定义规程";
  }
  if(strstr(name,"延度"))return australian?"":"JTG 3410—2025 / T 0605";
  return australian?"":"自定义规程";
}
const char* input_unit_label(const ResultRecord& r) {
  if(r.metric==Metric::Penetration)return "0.1 mm";
  if(r.metric==Metric::Softening)return "℃";
  if(r.metric==Metric::Mass)return "g";
  if(r.metric==Metric::Dynamic||r.metric==Metric::Rotational)
    return r.input_unit==ViscosityUnit::MilliPaS?"mPa·s":r.input_unit==ViscosityUnit::Poise?"P":"Pa·s";
  return r.custom_unit[0]?r.custom_unit:"—";
}
const char* result_rule_label(const ResultRecord& r) {
  if(r.metric!=Metric::Softening)return "按项目规则计算";
  if(r.softening_rule==SofteningRule::AustralianUnspecified)return "澳标软化点：请选择实际浴液";
  return r.softening_rule==SofteningRule::AustralianWater?"澳标水浴：均值修约至0.2℃":
    r.softening_rule==SofteningRule::AustralianGlycerol?"澳标甘油：均值修约至0.5℃":"国内：示数及均值按0.5℃修约";
}
void result_defaults(ResultRecord& r,const Run& run) {
  memset(&r,0,sizeof r);r.run_id=run.id;r.metric=detect_metric(run.name);r.group_count=1;
  snprintf(r.standard,sizeof r.standard,"%s",metric_standard(r.metric,false,run.name));
  strcpy(r.custom_unit,strstr(run.name,"延度")?"cm":"—");
  strcpy(r.custom_quantum,strstr(run.name,"延度")?"1":"0.001");
  if(r.metric==Metric::Penetration)strcpy(r.temperature,"25");
  if(r.metric==Metric::Dynamic)strcpy(r.temperature,"60");
  if(r.metric==Metric::Rotational)strcpy(r.temperature,"135");
  for(int g=0;g<kResultGroups;++g) {
    snprintf(r.groups[g].name,sizeof r.groups[g].name,"第%d组",g+1);
    r.groups[g].count=r.metric==Metric::Softening?2:3;
  }
}
bool parse_decimal(const char* text,double& value,bool negative) {
  if(!text||!*text)return false;
  const char* p=text;bool digit=false,dot=false;
  if(*p=='-'&&negative)++p;
  for(;*p;++p) {
    if(*p>='0'&&*p<='9')digit=true;
    else if(*p=='.'&&!dot)dot=true;
    else return false;
  }
  if(!digit)return false;
  value=strtod(text,nullptr);
  return isfinite(value)&&(negative||value>=0);
}
bool numeric_key(char* b,size_t size,char key,bool negative) {
  size_t n=strlen(b);
  if(key=='<'){if(n)b[n-1]=0;return true;}
  if(key=='C'){b[0]=0;return true;}
  if(key=='-') {
    if(!negative)return false;
    if(b[0]=='-'){memmove(b,b+1,n);return true;}
    if(n+1>=size)return false;
    memmove(b+1,b,n+1);b[0]='-';return true;
  }
  if(key=='.') {
    if(strchr(b,'.'))return false;
    if(!n||(n==1&&b[0]=='-')) {
      if(n+2>=size)return false;
      b[n++]='0';
    }
  } else if(key<'0'||key>'9')return false;
  if(n+1>=size)return false;
  b[n++]=key;b[n]=0;return true;
}
double round_even(double value,double quantum) {
  const double scaled=value/quantum,lower=floor(scaled),fraction=scaled-lower;
  return (fraction<.5-1e-9?lower:fraction>.5+1e-9?lower+1:fmod(lower,2)==0?lower:lower+1)*quantum;
}
bool result_has_data(const ResultRecord& r) {
  for(int g=0;g<r.group_count;++g)for(int i=0;i<r.groups[g].count;++i)if(r.groups[g].readings[i][0])return true;
  return false;
}
static ComputedResult bad(const char* reason) {
  ComputedResult out{};strcpy(out.text,"--");snprintf(out.reason,sizeof out.reason,"%s",reason);return out;
}
static double viscosity_factor(ViscosityUnit unit) {
  return unit==ViscosityUnit::MilliPaS?.001:unit==ViscosityUnit::Poise?.1:1;
}
ComputedResult calculate_result(const ResultRecord& r,int group) {
  if(r.metric==Metric::Softening&&r.softening_rule==SofteningRule::AustralianUnspecified)return bad("澳标软化点请先选择水浴或甘油浴");
  if(r.metric==Metric::Softening&&r.softening_rule==SofteningRule::Domestic) {
    char standard[64]={};size_t j=0;
    for(const char* p=r.standard;*p&&j<sizeof standard-1;++p)if(*p!=' ')standard[j++]=*p;
    if((strstr(standard,"AS")||strstr(standard,"as"))&&strstr(standard,"2341.18"))return bad("澳标软化点请先选择浴液规则");
  }
  if(group<0||group>=r.group_count)return bad("分组不存在");
  const ResultGroup& g=r.groups[group];
  double values[kReadings]={};int count=0;
  for(int i=0;i<g.count;++i) {
    if(!g.readings[i][0])return bad("请填齐本组读数");
    if(!parse_decimal(g.readings[i],values[i]))return bad("请输入有效非负数字");
    ++count;
  }
  if(!count)return bad("尚无读数");
  ComputedResult out{};out.valid=true;
  snprintf(out.unit,sizeof out.unit,"%s",input_unit_label(r));
  if(r.metric==Metric::Mass) {
    if(count!=3)return bad("请填写三项质量");
    if(values[1]<=values[0]||values[2]<values[0])return bad("请核对容器和总质量");
    double correction=0;
    if(g.correction[0]&&!parse_decimal(g.correction,correction,true))return bad("修正值无效");
    out.value=(values[2]-values[1])/(values[1]-values[0])*100+correction;
    strcpy(out.unit,"%");
    if(fabs(out.value)<.0005)strcpy(out.text,"0.000");
    else {const double display=copysign(floor(fabs(out.value)*1000+.5)/1000,out.value);snprintf(out.text,sizeof out.text,"%+.3f",display);}
    return out;
  }
  if(r.metric==Metric::Softening&&g.segregation) {
    if(count!=2)return bad("离析需要上部和下部读数");
    out.value=round(fabs(values[0]-values[1])*1000)/1000;
    snprintf(out.text,sizeof out.text,"%.3f",out.value);
    return out;
  }
  double total=0;
  for(int i=0;i<count;++i) {
    double v=values[i];
    if(r.metric==Metric::Softening&&r.softening_rule==SofteningRule::Domestic)v=round_even(v,.5);
    if(r.metric==Metric::Rotational)v*=viscosity_factor(r.input_unit);
    total+=v;
  }
  double mean=total/count;
  double custom=.001;if(r.metric==Metric::Other&&(!parse_decimal(r.custom_quantum,custom)||custom<.000001||custom>1000000))return bad("修约步长无效");
  const double quantum=r.metric==Metric::Softening?(r.softening_rule==SofteningRule::AustralianWater?.2:.5):
    r.metric==Metric::Rotational?.001:r.metric==Metric::Penetration||r.metric==Metric::Dynamic?1:custom;
  out.value=round_even(mean,quantum);
  if(r.metric==Metric::Rotational)strcpy(out.unit,"Pa·s");
  snprintf(out.text,sizeof out.text,r.metric==Metric::Softening?"%.1f":
    r.metric==Metric::Penetration||r.metric==Metric::Dynamic?"%.0f":"%.3f",out.value);
  if(r.metric==Metric::Other) {
    const char* dot=strchr(r.custom_quantum,'.');int decimals=dot?strlen(dot+1):0;
    if(decimals>6)decimals=6;
    snprintf(out.text,sizeof out.text,"%.*f",decimals,out.value);
  }
  return out;
}
bool compare_conditions(const ResultRecord& aged,const ResultRecord& base) {
  if(aged.metric!=base.metric||aged.softening_rule!=base.softening_rule||strcmp(aged.standard,base.standard))return false;
  double a=0,b=0;
  if(!aged.temperature[0]&&!base.temperature[0])return true;
  return parse_decimal(aged.temperature,a,true)&&parse_decimal(base.temperature,b,true)&&fabs(a-b)<1e-9;
}
ComputedResult calculate_comparison(const ResultRecord& aged,int group,const ResultRecord& base) {
  if(!compare_conditions(aged,base))return bad("对照的温度或规范不一致");
  if(group<0||group>=aged.group_count)return bad("分组不存在");
  if(aged.metric==Metric::Mass||aged.metric==Metric::Other)return bad("该项目无老化比值");
  const ResultGroup& g=aged.groups[group];int match=-1;
  for(int i=0;i<base.group_count;++i)if(!strcmp(g.name,base.groups[i].name)) {
    if(match>=0)return bad("对照组名重复");match=i;
  }
  if(match<0)return bad("没有同名对照组");
  if(g.segregation||base.groups[match].segregation)return bad("离析不参与老化软化点差");
  ComputedResult a=calculate_result(aged,group),b=calculate_result(base,match);
  if(!a.valid||!b.valid)return bad("请填齐老化前后读数");
  if(aged.metric==Metric::Dynamic){a.value*=viscosity_factor(aged.input_unit);b.value*=viscosity_factor(base.input_unit);}
  if(aged.metric!=Metric::Softening&&b.value<=0)return bad("老化前结果必须大于零");
  if((aged.metric==Metric::Dynamic||aged.metric==Metric::Rotational)&&a.value<=0)return bad("黏度必须大于零");
  ComputedResult out{};out.valid=true;
  const bool difference=aged.metric==Metric::Softening,ratio=aged.metric==Metric::Dynamic||aged.metric==Metric::Rotational;
  out.value=difference?a.value-b.value:a.value/b.value*(ratio?1:100);
  out.value=round(out.value*100)/100;
  strcpy(out.unit,difference?"℃":ratio?"倍":"%");
  snprintf(out.text,sizeof out.text,"%.2f",out.value);
  return out;
}
int results_math_selftest() {
  int errors=0;Run run{};run.id=1;strcpy(run.name,"软化点");ResultRecord r{};result_defaults(r,run);
  auto test=[&](const char* a,const char* b,double expected){strcpy(r.groups[0].readings[0],a);strcpy(r.groups[0].readings[1],b);auto v=calculate_result(r,0);if(!v.valid||fabs(v.value-expected)>1e-7)++errors;};
  test("63.49","63.96",64);test("63.25","63.25",63);test("63.75","63.75",64);
  test("63","63.5",63);test("63.5","64",64);test("64","64.5",64);test("64.5","65",65);
  r.groups[0].segregation=true;test("63.49","63.96",.47);r.groups[0].segregation=false;
  r.softening_rule=SofteningRule::AustralianWater;test("63.49","63.96",63.8);
  r.softening_rule=SofteningRule::AustralianGlycerol;test("63.49","63.96",63.5);
  r.softening_rule=SofteningRule::AustralianUnspecified;if(calculate_result(r,0).valid)++errors;
  r.softening_rule=SofteningRule::Domestic;
  r.metric=Metric::Mass;r.groups[0].count=3;
  strcpy(r.groups[0].readings[0],"137.5990");strcpy(r.groups[0].readings[1],"172.5527");strcpy(r.groups[0].readings[2],"172.5769");
  if(strcmp(calculate_result(r,0).text,"+0.069"))++errors;
  strcpy(r.groups[0].correction,"0.006");if(strcmp(calculate_result(r,0).text,"+0.075"))++errors;
  r.groups[0].correction[0]=0;strcpy(r.groups[0].readings[2],"172.5285");if(strcmp(calculate_result(r,0).text,"-0.069"))++errors;
  strcpy(r.groups[0].readings[0],"172.5527");if(calculate_result(r,0).valid)++errors;
  strcpy(r.groups[0].readings[0],"0");strcpy(r.groups[0].readings[1],"200");strcpy(r.groups[0].readings[2],"200.125");if(strcmp(calculate_result(r,0).text,"+0.063"))++errors;
  strcpy(r.groups[0].readings[2],"199.875");if(strcmp(calculate_result(r,0).text,"-0.063"))++errors;
  r.metric=Metric::Rotational;r.input_unit=ViscosityUnit::MilliPaS;r.groups[0].count=1;strcpy(r.groups[0].readings[0],"1250");
  if(fabs(calculate_result(r,0).value-1.25)>1e-9)++errors;
  r.metric=Metric::Penetration;r.softening_rule=SofteningRule::Domestic;strcpy(r.groups[0].readings[0],"50");
  ResultRecord aged=r;strcpy(aged.groups[0].readings[0],"40");if(fabs(calculate_comparison(aged,0,r).value-80)>1e-9)++errors;
  strcpy(r.groups[0].readings[0],"0");if(calculate_comparison(aged,0,r).valid)++errors;
  r.metric=aged.metric=Metric::Dynamic;r.input_unit=ViscosityUnit::Poise;aged.input_unit=ViscosityUnit::PaS;
  strcpy(r.groups[0].readings[0],"1000");strcpy(aged.groups[0].readings[0],"270");if(fabs(calculate_comparison(aged,0,r).value-2.7)>1e-9)++errors;
  strcpy(aged.temperature,"80");if(calculate_comparison(aged,0,r).valid)++errors;
  double value=0;if(parse_decimal("",value)||parse_decimal("-1",value)||parse_decimal("1e3",value)||parse_decimal("1.2.3",value))++errors;
  if(!parse_decimal("0",value)||!parse_decimal("-.5",value,true))++errors;
  char entry[16]={};numeric_key(entry,sizeof entry,'.',true);numeric_key(entry,sizeof entry,'5',true);numeric_key(entry,sizeof entry,'-',true);
  if(strcmp(entry,"-0.5")||numeric_key(entry,sizeof entry,'.',true))++errors;
  numeric_key(entry,sizeof entry,'<',true);numeric_key(entry,sizeof entry,'C',true);if(entry[0])++errors;
  return errors;
}
