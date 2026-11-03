// Included by the sketch after the common drawing helpers.
#include "qrcode.h"
ResultRecord result_draft{};
int result_group=0,numeric_field=0;
char number_entry[kNumericSize]={};
bool number_negative=false,export_times=false,qr_wifi=true;
uint32_t export_selection=0;
uint32_t shared_scope=0;
int export_date=-1;
uint32_t export_sample_id=0;
Page export_return=Page::ExportDates;
String exported_path;
int summary_page=0;
uint8_t qr_modules[41*41]={};int qr_side=0;

bool load_result(int run_index) {
  if(run_index<0||run_index>=lab.run_count)return false;
  const ResultRecord* stored=results_for(lab.runs[run_index]);
  if(!stored){notice("读数存储不可用");return false;}
  selected_run=run_index;result_draft=*stored;result_group=0;return true;
}
bool commit_result() {
  const int index=lab_find_run(result_draft.run_id);
  if(index<0){notice("项目已不存在");return false;}
  selected_run=index;
  result_draft.modified=now_ms();
  if(!results_save(result_draft)) {
    const auto* stored=results_for(lab.runs[selected_run]);if(stored)result_draft=*stored;
    notice("数据未保存，请检查存储");return false;
  }
  result_draft=*results_for(lab.runs[selected_run]);
  if(!records_ok())notice("板内已保存，TF 待同步");
  return true;
}
const char* reading_label(const ResultRecord& r,int index) {
  static const char* normal[]={"读数1","读数2","读数3","读数4","读数5","读数6"};
  if(r.metric==Metric::Mass){static const char* mass[]={"空容器质量","老化前总质量","老化后总质量"};return index==6?"质量修正 %":mass[index];}
  if(r.metric==Metric::Softening&&r.groups[result_group].segregation)return index==0?"上部软化点":"下部软化点";
  return normal[index];
}
void open_number(int field) {
  numeric_field=field;number_negative=field==7||field==6;
  const char* text=field==8?result_draft.custom_quantum:field==7?result_draft.temperature:field==6?result_draft.groups[result_group].correction:result_draft.groups[result_group].readings[field];
  snprintf(number_entry,sizeof number_entry,"%s",text);page=Page::NumericPad;
}
void open_result_text(EditTarget target) {
  edit_target=target;
  const char* text=target==EditTarget::ResultGroupName?result_draft.groups[result_group].name:target==EditTarget::ResultStandard?result_draft.standard:target==EditTarget::ResultUnit?result_draft.custom_unit:result_draft.notes;
  snprintf(entry,sizeof entry,"%s",text);page=Page::EditText;start_pinyin_name();
}
bool save_result_text() {
  if(pinyin_mode&&pinyin_input[0]){notice("请先选拼音候选字");return false;}
  if(!entry[0]&&edit_target!=EditTarget::ResultNotes){notice("名称不能为空");return false;}
  if(edit_target==EditTarget::ResultGroupName) {
    if(strlen(entry)>=sizeof result_draft.groups[0].name){notice("组名过长");return false;}
    for(int i=0;i<result_draft.group_count;++i)if(i!=result_group&&!strcmp(entry,result_draft.groups[i].name)){notice("组名不能重复");return false;}
    strcpy(result_draft.groups[result_group].name,entry);
  }else if(edit_target==EditTarget::ResultUnit){if(strlen(entry)>=sizeof result_draft.custom_unit){notice("单位名称过长");return false;}strcpy(result_draft.custom_unit,entry);}
  else if(edit_target==EditTarget::ResultStandard) {
    snprintf(result_draft.standard,sizeof result_draft.standard,"%s",entry);
    if(result_draft.metric==Metric::Softening) {
      if(strstr(entry,"AS")||strstr(entry,"as")){if(result_draft.softening_rule==SofteningRule::Domestic)result_draft.softening_rule=SofteningRule::AustralianUnspecified;}
      else result_draft.softening_rule=SofteningRule::Domestic;
    }
  }
  else snprintf(result_draft.notes,sizeof result_draft.notes,"%s",entry);
  bool ok=commit_result();if(ok)page=edit_target==EditTarget::ResultGroupName?Page::Results:Page::ResultOptions;return ok;
}
void draw_results() {
  selected_run=lab_find_run(result_draft.run_id);
  if(selected_run<0||selected_run>=lab.run_count)return;
  const Run& run=lab.runs[selected_run];const auto& g=result_draft.groups[result_group];
  title_bar(run.name,run.sample);
  button(18,75,258,45,g.name,Action::ResultGroupName,0,BLUE);
  button(286,75,48,45,"<",Action::ResultGroup,-1);
  button(342,75,48,45,">",Action::ResultGroup,1);
  button(400,75,90,45,"加组",Action::ResultAddGroup);
  button(500,75,112,45,"加读数",Action::ResultAddReading);
  button(622,75,160,45,"参数／模式",Action::ResultOptions,0,BLUE);
  for(int i=0;i<g.count;++i) {
    const int x=18+(i%2)*390,y=131+(i/2)*63;
    char text[100];snprintf(text,sizeof text,"%s：%s %s",reading_label(result_draft,i),g.readings[i][0]?g.readings[i]:"--",input_unit_label(result_draft));
    button(x,y,374,55,text,Action::ResultNumber,i,BLUE);
  }
  if(result_draft.metric==Metric::Mass) {
    char text[80];snprintf(text,sizeof text,"质量修正：%s %%",g.correction[0]?g.correction:"0");
    button(408,194,374,55,text,Action::ResultNumber,6,BLUE);
  }
  auto result=calculate_result(result_draft,result_group);
  char text[120];snprintf(text,sizeof text,"%s：%s %s",result_draft.metric==Metric::Mass?"质量变化":g.segregation?"离析差值":"结果",result.text,result.unit);
  label_fitted(22,330,755,result.valid?text:result.reason,result.valid?CYAN:ORANGE);
  if(run.phase!=AgingPhase::Original&&result_draft.metric!=Metric::Mass&&result_draft.metric!=Metric::Other) {
    auto derived=result_derived(run,result_group);
    snprintf(text,sizeof text,"%s：%s %s",result_draft.metric==Metric::Softening?"软化点变化":result_draft.metric==Metric::Penetration?"针入度比":"黏度比",derived.text,derived.unit);
    label_fitted(22,365,755,derived.valid?text:derived.reason,derived.valid?GREEN:MUTED);
  }else label_fitted(22,365,755,result_rule_label(result_draft),MUTED);
  nav();
}
void draw_result_options() {
  selected_run=lab_find_run(result_draft.run_id);if(selected_run<0){page=Page::Sample;return;}
  title_bar("试验参数",lab.runs[selected_run].name);
  char text[120];
  snprintf(text,sizeof text,"数据类型：%s",metric_label(result_draft.metric));button(18,77,374,49,text,Action::ResultMetric);
  snprintf(text,sizeof text,"输入单位：%s",input_unit_label(result_draft));button(408,77,374,49,text,Action::ResultUnit);
  snprintf(text,sizeof text,"温度：%s ℃",result_draft.temperature[0]?result_draft.temperature:"未填");button(18,139,374,49,text,Action::ResultNumber,7);
  button(408,139,374,49,result_draft.standard,Action::ResultStandard);
  button(18,201,374,49,result_draft.groups[result_group].segregation?"模式：离析测试":"模式：普通测试",Action::ResultMode);
  if(result_draft.metric==Metric::Other){snprintf(text,sizeof text,"修约步长：%s",result_draft.custom_quantum);button(408,201,374,49,text,Action::ResultNumber,8);}
  else button(408,201,374,49,result_draft.softening_rule==SofteningRule::Domestic?"浴液／规则：国内":result_draft.softening_rule==SofteningRule::AustralianWater?"浴液：水浴":result_draft.softening_rule==SofteningRule::AustralianGlycerol?"浴液：甘油":"浴液：请选择",Action::ResultRule);
  char reason[96];const Run* base=result_baseline(lab.runs[selected_run],reason,sizeof reason);
  snprintf(text,sizeof text,"对照项目：%s%u",base?"ID ":"未选 ",base?base->id:0);button(18,263,374,49,text,Action::ResultBaseline);
  button(408,263,374,49,"编辑备注",Action::ResultNotes);
  button(18,325,374,49,"减少末条空读数",Action::ResultRemoveReading);
  button(408,325,374,49,"删除当前空分组",Action::ResultRemoveGroup,0,RED);
  label_fitted(22,386,750,result_rule_label(result_draft),MUTED);nav();
}
void draw_result_standards() {
  title_bar("选择采用规范",lab.runs[selected_run].name);
  const char* domestic=metric_standard(result_draft.metric,false,lab.runs[selected_run].name);
  const char* australian=metric_standard(result_draft.metric,true,lab.runs[selected_run].name);
  label(22,82,"国内规范",CYAN);button(18,116,764,60,domestic,Action::StandardChoose,0,BLUE);
  if(australian[0]){label(22,196,"澳洲规范",CYAN);button(18,230,764,60,australian,Action::StandardChoose,1,BLUE);}
  button(18,324,350,60,"自定义／编辑规范",Action::StandardCustom,0,BLUE);
  nav();
}
void draw_numeric_pad() {
  const char* name=numeric_field==8?"修约步长":numeric_field==7?"温度 ℃":reading_label(result_draft,numeric_field);
  title_bar("录入数值",name);
  box(18,73,764,48,CARD);label(35,84,number_entry[0]?number_entry:"请输入数字",CYAN);
  const char* keys[]={"1","2","3","4","5","6","7","8","9","±","0","."};
  for(int i=0;i<12;++i)button(18+(i%3)*182,132+(i/3)*67,170,58,keys[i],Action::NumberKey,i==9?'-':i==11?'.':i==10?'0':'1'+i,KEY_BG);
  button(575,132,207,58,"退格",Action::NumberKey,'<');
  button(575,199,207,58,"清空",Action::NumberKey,'C');
  button(575,266,207,58,"确认",Action::NumberConfirm,0,GREEN);
  button(575,333,207,58,"确认并下一项",Action::NumberConfirm,1,BLUE);
  nav();
}
void draw_result_summary() {
  if(selected_sample<0||selected_sample>=lab.sample_count)return;
  title_bar("结果汇总",lab.samples[selected_sample].name);
  int found=0,shown=0;
  for(int i=0;i<lab.run_count;++i) {
    const Run& run=lab.runs[i];if(run.sample_id!=lab.samples[selected_sample].id)continue;
    const auto* r=results_for(run);if(!r)continue;
    for(int g=0;g<r->group_count;++g) {
      if(found++<summary_page*4)continue;
      if(shown>=4)continue;
      const int y=79+shown++*74;char text[150];auto result=calculate_result(*r,g);
      box(18,y,764,68,CARD);
      snprintf(text,sizeof text,"%s · %s · %s",phase_label(run.phase),run.name,r->groups[g].name);label_fitted(28,y+3,740,text);
      auto derived=result_derived(run,g);
      if(derived.valid)snprintf(text,sizeof text,"结果 %s %s；%s %s %s",result.text,result.unit,r->metric==Metric::Softening?"变化":r->metric==Metric::Penetration?"针入度比":"黏度比",derived.text,derived.unit);
      else snprintf(text,sizeof text,"结果 %s %s",result.text,result.unit);
      label_fitted(28,y+35,730,text,result.valid?CYAN:MUTED);
      hit(18,y,764,68,Action::SummaryOpen,i*kResultGroups+g);
    }
  }
  if(!found)label(28,150,"请先添加实验项目",MUTED);
  page_control(summary_page,max(1,(found+3)/4),474,380,308,36);
  nav();
}
int export_count(uint32_t mask) {int count=0;while(mask){count+=mask&1;mask>>=1;}return count;}
uint32_t export_sample_mask(uint32_t id) {
  uint32_t mask=0;for(int i=0;i<lab.run_count;++i)if(lab.runs[i].sample_id==id)mask|=uint32_t(1)<<i;return mask;
}
uint32_t export_date_mask(int key) {
  uint32_t mask=0;for(int i=0;i<lab.sample_count;++i)if(sample_date_key(lab.samples[i])==key)mask|=export_sample_mask(lab.samples[i].id);return mask;
}
int export_sample_run_count(){return export_count(export_sample_mask(export_sample_id));}
void export_folder_controls() {
  button(18,75,200,45,"清空选择",Action::ExportScope,2);
  button(230,75,214,45,"手机上选择",Action::MobileShare,0,BLUE);
  char text[64];snprintf(text,sizeof text,"传输所选（%d项）",export_count(export_selection));
  button(456,75,326,45,text,Action::ExportGenerate,0,GREEN);
}
void draw_export_dates() {
  title_bar("导出：选择日期");export_folder_controls();
  int keys[kMaxSamples]={};const int count=sample_dates(keys);list_page=constrain(list_page,0,max(0,(count-1)/4));
  const int start=list_page*4;
  for(int row=0;row<4&&start+row<count;++row) {
    const int key=keys[start+row],y=132+row*69;const uint32_t mask=export_date_mask(key);
    box(18,y,764,62,CARD);char text[80];date_label(key,text,sizeof text);label(30,y+5,text);
    snprintf(text,sizeof text,"%d 个样品 · 已选 %d/%d 项",samples_on_date(key),export_count(export_selection&mask),export_count(mask));label(30,y+33,text,MUTED);
    button(490,y+11,130,40,mask&&(export_selection&mask)==mask?"取消整日":"选择整日",Action::ExportDateToggle,key,BLUE);
    button(635,y+11,130,40,"选择样品",Action::ExportDateOpen,key,GREEN);
    hit(18,y,450,62,Action::ExportDateOpen,key);
  }
  if(!count)label(28,170,"还没有样品记录",MUTED);
  page_control(list_page,max(1,(count+3)/4),490,9,172,46);nav();
}
void draw_export_samples() {
  char heading[40];date_label(export_date,heading,sizeof heading);title_bar(heading,"选择样品");export_folder_controls();
  const int count=samples_on_date(export_date);list_page=constrain(list_page,0,max(0,(count-1)/4));const int start=list_page*4;
  int found=0,shown=0;
  for(int i=lab.sample_count-1;i>=0&&shown<4;--i) {
    const Sample& sample=lab.samples[i];if(sample_date_key(sample)!=export_date)continue;if(found++<start)continue;
    const int y=132+shown++*69;const uint32_t mask=export_sample_mask(sample.id);box(18,y,764,62,CARD);label_fitted(30,y+5,440,sample.name);
    char text[80];snprintf(text,sizeof text,"已选 %d/%d 项试验",export_count(export_selection&mask),export_count(mask));label(30,y+33,text,MUTED);
    button(490,y+11,130,40,mask&&(export_selection&mask)==mask?"取消样品":"整个样品",Action::ExportSampleToggle,i,BLUE);
    button(635,y+11,130,40,"选择试验",Action::ExportSampleOpen,i,GREEN);
    hit(18,y,450,62,Action::ExportSampleOpen,i);
  }
  if(!count)label(28,170,"该日期没有样品",MUTED);
  page_control(list_page,max(1,(count+3)/4),490,9,172,46);nav();
}
void draw_export_select() {
  const int sample_index=lab_find_sample(export_sample_id);
  if(sample_index<0){page=Page::ExportSamples;draw_export_samples();return;}
  title_bar(lab.samples[sample_index].name,"勾选试验");
  button(18,75,190,43,"本样品全选",Action::ExportScope,3);
  button(220,75,190,43,"取消本样品",Action::ExportScope,4);
  button(422,75,360,43,export_times?"附带步骤时间：开":"附带步骤时间：关",Action::ExportTimes);
  const int count=export_sample_run_count();list_page=constrain(list_page,0,max(0,(count-1)/4));const int start=list_page*4;
  int found=0,shown=0;
  for(int i=0;i<lab.run_count&&shown<4;++i) {
    const Run& run=lab.runs[i];if(run.sample_id!=export_sample_id)continue;if(found++<start)continue;
    const int y=128+shown++*58;char text[150];snprintf(text,sizeof text,"%s %s · %s",export_selection&(uint32_t(1)<<i)?"[√]":"[ ]",phase_label(run.phase),run.name);
    button(18,y,764,50,text,Action::ExportToggle,i,export_selection&(uint32_t(1)<<i)?GREEN:BLUE);
  }
  if(!count)label(28,170,"样品尚无试验项目",MUTED);
  char text[64];snprintf(text,sizeof text,"传输所选（%d项）",export_count(export_selection));
  button(18,370,270,45,text,Action::ExportGenerate,0,GREEN);
  button(300,370,174,45,"手机上选择",Action::MobileShare,0,BLUE);
  button(490,375,292,40,"继续选择其他日期",Action::ExportRoot,0,BLUE);
  page_control(list_page,max(1,(count+3)/4),490,9,172,46);nav();
}
void copy_qr(esp_qrcode_handle_t code) {
  qr_side=esp_qrcode_get_size(code);if(qr_side>41){qr_side=0;return;}
  for(int y=0;y<qr_side;++y)for(int x=0;x<qr_side;++x)qr_modules[y*41+x]=esp_qrcode_get_module(code,x,y);
}
void prepare_transfer_qr() {
  esp_qrcode_config_t config=ESP_QRCODE_CONFIG_DEFAULT();config.display_func=copy_qr;config.max_qrcode_version=6;
  const String payload=qr_wifi?transfer_wifi_qr():transfer_address();
  if(esp_qrcode_generate(&config,payload.c_str())!=ESP_OK)qr_side=0;
}
void draw_transfer() {
  title_bar("手机传输",transfer_active()?"热点已开启":"热点已关闭");
  if(!transfer_active()){label(24,110,transfer_report_only()?"开启热点后下载历史报告":"开启热点后可在手机选择试验数据",CYAN);button(24,180,450,65,"重新开启传输热点",Action::TransferStart,0,GREEN);nav();return;}
  label(22,83,"1. 手机连接以下 Wi-Fi",CYAN);label(22,121,transfer_ssid());
  char text[80];snprintf(text,sizeof text,"密码：%s",transfer_password());label(22,160,text);
  label(22,204,transfer_report_only()?"2. 手机点击下载历史报告":"2. 手机勾选样品和试验项目",CYAN);label(22,242,"自动弹出，或点登录网络提示");
  snprintf(text,sizeof text,transfer_report_only()?"历史项目；已完成 %u 次下载":"可选 %u 项；已完成 %u 次下载",transfer_report_only()?transfer_downloads():transfer_scope_count(),transfer_downloads());label_fitted(22,286,450,text,MUTED);
  label(22,326,transfer_address().c_str(),MUTED);
  button(22,368,250,45,"关闭热点",Action::TransferStop,0,RED);
  button(494,365,288,48,qr_wifi?"切换下载二维码":"切换连接二维码",Action::TransferQR);
  const int scale=qr_side?(270/(qr_side+8)):1,x=504,y=88;
  box(x,y,270,270,WHITE_);
  for(int yy=0;yy<qr_side;++yy)for(int xx=0;xx<qr_side;++xx)if(qr_modules[yy*41+xx])box(x+(xx+4)*scale,y+(yy+4)*scale,scale,scale,KEY_BG);
  nav();
}
bool handle_result_action(Action action,int arg) {
  switch(action) {
    case Action::ResultOpen: if(load_result(arg))page=Page::Results;break;
    case Action::SummaryOpen: if(load_result(arg/kResultGroups)){result_group=arg%kResultGroups;page=Page::Results;}break;
    case Action::ResultGroupName:open_result_text(EditTarget::ResultGroupName);break;
    case Action::ResultStandard:page=Page::ResultStandards;break;
    case Action::StandardCustom:open_result_text(EditTarget::ResultStandard);break;
    case Action::StandardChoose: {
      const char* value=metric_standard(result_draft.metric,arg==1,lab.runs[selected_run].name);
      if(!value[0])break;
      snprintf(result_draft.standard,sizeof result_draft.standard,"%s",value);
      if(result_draft.metric==Metric::Softening) {
        if(arg==0)result_draft.softening_rule=SofteningRule::Domestic;
        else if(result_draft.softening_rule==SofteningRule::Domestic)result_draft.softening_rule=SofteningRule::AustralianUnspecified;
      }
      if(commit_result())page=Page::ResultOptions;break;
    }
    case Action::ResultNotes:open_result_text(EditTarget::ResultNotes);break;
    case Action::ResultGroup:result_group=constrain(result_group+arg,0,int(result_draft.group_count)-1);break;
    case Action::ResultAddGroup:
      if(result_draft.group_count>=kResultGroups)notice("每项最多四组");
      else {
        result_group=result_draft.group_count;
        auto& group=result_draft.groups[result_group];memset(&group,0,sizeof group);
        group.count=result_draft.metric==Metric::Softening?2:3;
        for(int number=1;number<=100;++number) {
          snprintf(group.name,sizeof group.name,"第%d组",number);bool used=false;
          for(int i=0;i<result_draft.group_count;++i)if(!strcmp(group.name,result_draft.groups[i].name))used=true;
          if(!used)break;
        }
        ++result_draft.group_count;if(!commit_result())result_group=0;
      }break;
    case Action::ResultRemoveGroup: {
      if(result_draft.group_count<=1){notice("请至少保留一组");break;}
      bool has=false;for(auto& v:result_draft.groups[result_group].readings)if(v[0])has=true;
      if(has){notice("请先清空本组读数");break;}
      for(int i=result_group;i<result_draft.group_count-1;++i)result_draft.groups[i]=result_draft.groups[i+1];
      --result_draft.group_count;result_group=0;commit_result();break;
    }
    case Action::ResultAddReading:
      if(result_draft.metric==Metric::Mass||result_draft.metric==Metric::Softening)notice("该类型使用固定读数项");
      else if(result_draft.groups[result_group].count>=kReadings)notice("每组最多六条读数");
      else {++result_draft.groups[result_group].count;commit_result();}break;
    case Action::ResultRemoveReading: {
      auto& g=result_draft.groups[result_group];
      if(result_draft.metric==Metric::Mass||result_draft.metric==Metric::Softening||g.count<=1)notice("请保留所需读数项");
      else if(g.readings[g.count-1][0])notice("请先清空最后一条读数");
      else {--g.count;commit_result();}break;
    }
    case Action::ResultOptions:page=Page::ResultOptions;break;
    case Action::ResultNumber:open_number(arg);break;
    case Action::NumberKey:if(!numeric_key(number_entry,sizeof number_entry,char(arg),number_negative))notice("该字符无法输入");break;
    case Action::NumberConfirm: {
      double value=0;if(number_entry[0]&&!parse_decimal(number_entry,value,number_negative)){notice("请输入有效数字");break;}
      if(numeric_field==8&&(!number_entry[0]||value<.000001||value>1000000)){notice("步长须为0.000001至1000000");break;}
      char* dest=numeric_field==8?result_draft.custom_quantum:numeric_field==7?result_draft.temperature:numeric_field==6?result_draft.groups[result_group].correction:result_draft.groups[result_group].readings[numeric_field];
      strcpy(dest,number_entry);
      if(!commit_result())break;
      if(arg&&numeric_field<result_draft.groups[result_group].count-1)open_number(numeric_field+1);
      else page=numeric_field>=7?Page::ResultOptions:Page::Results;break;
    }
    case Action::ResultMetric:
      if(result_has_data(result_draft)){notice("已有读数，请先清空再改类型");break;}
      result_draft.metric=Metric((unsigned(result_draft.metric)+1)%6);
      snprintf(result_draft.standard,sizeof result_draft.standard,"%s",metric_standard(result_draft.metric,false,lab.runs[selected_run].name));
      result_draft.softening_rule=SofteningRule::Domestic;result_draft.baseline_id=0;
      for(auto& g:result_draft.groups){g.count=result_draft.metric==Metric::Softening?2:3;g.segregation=false;g.correction[0]=0;}
      commit_result();break;
    case Action::ResultUnit:
      if(result_draft.metric==Metric::Other){open_result_text(EditTarget::ResultUnit);break;}
      if(result_draft.metric!=Metric::Dynamic&&result_draft.metric!=Metric::Rotational){notice("该类型单位固定");break;}
      if(result_has_data(result_draft)){notice("已有读数，请先清空再改单位");break;}
      result_draft.input_unit=ViscosityUnit((unsigned(result_draft.input_unit)+1)%3);commit_result();break;
    case Action::ResultMode:
      if(result_draft.metric!=Metric::Softening){notice("仅软化点可选普通或离析");break;}
      result_draft.groups[result_group].segregation=!result_draft.groups[result_group].segregation;commit_result();break;
    case Action::ResultRule:
      if(result_draft.metric!=Metric::Softening){notice("此处为软化点修约规则");break;}
      if(result_draft.softening_rule==SofteningRule::Domestic){notice("国内按0.5℃修约，澳标请先选规范");break;}
      result_draft.softening_rule=result_draft.softening_rule==SofteningRule::AustralianUnspecified?SofteningRule::AustralianWater:
        result_draft.softening_rule==SofteningRule::AustralianWater?SofteningRule::AustralianGlycerol:SofteningRule::AustralianUnspecified;
      commit_result();break;
    case Action::ResultBaseline: {
      uint32_t ids[kMaxRuns]={};int n=result_baselines(lab.runs[selected_run],ids,kMaxRuns);
      if(!n){notice("请先建立同条件未老化项目");break;}
      int next=0;for(int i=0;i<n;++i)if(ids[i]==result_draft.baseline_id)next=(i+1)%n;
      result_draft.baseline_id=ids[next];commit_result();break;
    }
    case Action::ResultSummary:summary_page=0;page=Page::ResultSummary;break;
    case Action::SummaryPage:summary_page=max(0,summary_page+arg);break;
    case Action::ExportOpen:page=Page::ExportDates;list_page=0;export_selection=0;break;
    case Action::ExportRoot:page=Page::ExportDates;list_page=0;break;
    case Action::ExportDateOpen:export_date=arg;page=Page::ExportSamples;list_page=0;break;
    case Action::ExportSampleOpen:export_sample_id=lab.samples[arg].id;page=Page::ExportSelect;list_page=0;break;
    case Action::ExportDateToggle: {
      uint32_t mask=export_date_mask(arg);if(!mask){notice("该日期暂无试验项目");break;}
      if((export_selection&mask)==mask)export_selection&=~mask;else export_selection|=mask;break;
    }
    case Action::ExportSampleToggle: {
      uint32_t mask=export_sample_mask(lab.samples[arg].id);if(!mask){notice("样品暂无试验项目");break;}
      if((export_selection&mask)==mask)export_selection&=~mask;else export_selection|=mask;break;
    }
    case Action::ExportToggle: {
      uint32_t bit=uint32_t(1)<<arg;export_selection^=bit;
      if(export_selection&bit){char reason[96];const Run* base=result_baseline(lab.runs[arg],reason,sizeof reason);if(base){int i=lab_find_run(base->id);if(!(export_selection&(uint32_t(1)<<i)))notice("需要比值时请另选未老化对照");}}
      break;
    }
    case Action::ExportScope:
      if(arg==3){export_selection|=export_sample_mask(export_sample_id);break;}
      if(arg==4){export_selection&=~export_sample_mask(export_sample_id);break;}
      export_selection=0;
      for(int i=0;i<lab.run_count;++i){const auto* r=results_for(lab.runs[i]);if(arg==1||(arg==0&&r&&result_has_data(*r)))export_selection|=uint32_t(1)<<i;}break;
    case Action::ExportTimes:export_times=!export_times;break;
    case Action::ExportGenerate:
      if(!export_selection){notice("请先勾选实验项目");break;}
      if(!result_export(export_selection,export_times,exported_path)){notice("导出失败，请检查 TF 卡或减少项目");break;}
      shared_scope=export_selection;
      export_return=page;
      if(!transfer_start(exported_path,shared_scope)){notice("报告已存卡，热点开启失败");page=Page::Transfer;break;}
      qr_wifi=true;prepare_transfer_qr();page=Page::Transfer;break;
    case Action::MobileShare:
      export_return=page;
      shared_scope=lab.run_count==32?0xffffffffu:(uint32_t(1)<<lab.run_count)-1;exported_path="";
      if(!shared_scope){notice("还没有试验项目");break;}
      if(!transfer_start_scope(shared_scope)){notice("热点开启失败");break;}
      qr_wifi=true;prepare_transfer_qr();page=Page::Transfer;break;
    case Action::TransferStart:
      if(!(transfer_report_only()?transfer_start_report(exported_path):exported_path.length()?transfer_start(exported_path,shared_scope):transfer_start_scope(shared_scope)))notice("开启失败，请返回重新选择");else {qr_wifi=true;prepare_transfer_qr();}break;
    case Action::TransferStop:transfer_stop();break;
    case Action::TransferQR:qr_wifi=!qr_wifi;prepare_transfer_qr();break;
    default:return false;
  }
  redraw=true;return true;
}
