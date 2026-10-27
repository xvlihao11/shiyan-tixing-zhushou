// BBR stores S and m separately. Never average these different quantities together.
function isBBR(run){return run?.resultKind==='bbr60'||/\bBBR\b|弯曲梁|弯曲蠕变/i.test(String(run?.name||''))}
const BBR_STANDARD='JTG 3410—2025 / T 0627—2011';
function ensureBBRTemplate(){
  // A legacy single-metric bending test must not hide the editable paired-result template.
  let template=(db.templates||[]).find(t=>t.kind!=='heating'&&(t.resultKind==='bbr60'||/^BBR(?:低温)?(?:试验|实验)?$/i.test(String(t.name||'').replace(/\s/g,''))));
  let changed=false;
  if(!template){template={id:uid(),resultKind:'bbr60',name:'BBR低温试验',standard:BBR_STANDARD,unit:'S: MPa；m: —',temps:'-6,-12,-18,-24,-30,-36,0',initialReadings:true,steps:[{name:'试件准备',minutes:0,mode:'none'},{name:'低温恒温（按实际规程核对）',minutes:0,mode:'none'},{name:'测定与录入 60 s 结果',minutes:0,mode:'none'}]};db.templates.push(template);changed=true}
  else if(template.resultKind!=='bbr60'){template.resultKind='bbr60';changed=true}
  if(changed)localStorage.setItem(KEY,JSON.stringify(db));
  return template;
}
function openBBRTemplate(){const template=ensureBBRTemplate();openTemplateId=template.id;render();get('templateCard_'+template.id)?.scrollIntoView?.({behavior:'smooth',block:'start'})}
function bbrSpecimenMode(group){return group.bbrMode==='specimens'}
function bbrValidPair(values){return values.length===2&&values.every(v=>String(v??'').trim()!==''&&Number.isFinite(Number(v)))&&Number(values[0])>0&&Number(values[1])>=0}
function bbrValues(group){
  const values=group.readings||[],count=bbrSpecimenMode(group)?3:1;
  if(values.length!==count*2)return null;
  const pairs=Array.from({length:count},(_,i)=>values.slice(i*2,i*2+2));
  if(!pairs.every(bbrValidPair))return null;
  // T 0627 sections 6.6–6.7: separate specimen values and separate means.
  return {s:roundResult(pairs.reduce((n,p)=>n+roundResult(Number(p[0]),.1),0)/count,.1).toFixed(1),m:roundResult(pairs.reduce((n,p)=>n+roundResult(Number(p[1]),.01),0)/count,.01).toFixed(2)};
}
function bbrResultText(group){const v=bbrValues(group);return v?`S(60 s) = ${v.s} MPa；m(60 s) = ${v.m}`:'—'}
function bbrReadingText(group,metric=null){
  return Array.from({length:bbrSpecimenMode(group)?3:1},(_,i)=>{
    const pair=(group.readings||[]).slice(i*2,i*2+2),prefix=bbrSpecimenMode(group)?`试件 ${i+1}：`:'';
    return prefix+(metric==='s'?`S(60 s) = ${pair[0]||'—'} MPa`:metric==='m'?`m(60 s) = ${pair[1]||'—'}`:`S(60 s) = ${pair[0]||'—'} MPa；m(60 s) = ${pair[1]||'—'}`);
  }).join('；');
}
function setBBRMode(rid,gid,mode){
  const run=db.runs.find(r=>r.id===rid),group=run?.groups.find(g=>g.id===gid);if(!run||!group||!['summary','specimens'].includes(mode))return;
  collectResults(run);
  if((group.readings||[]).some(v=>String(v).trim()!=='')&&!confirm('切换录入方式会清空本组当前读数，请先保留原始记录。确定切换？'))return render();
  group.bbrMode=mode;group.readings=Array(bbrSpecimenMode(group)?6:2).fill('');save();
}
function bbrFieldsUI(run,group){
  const specimens=bbrSpecimenMode(group);
  return `<label>BBR 结果录入方式</label><select aria-label="BBR 结果录入方式" onchange="setBBRMode('${run.id}','${group.id}',this.value)"><option value="summary" ${specimens?'':'selected'}>仪器最终结果 · S 与 m</option><option value="specimens" ${specimens?'selected':''}>三个试件原始值 · 分别取平均</option></select><p class="muted">只填写 60 s 的结果。${specimens?'三个试件的 S、m 各自取平均；S 保留 1 位小数，m 保留 2 位小数。':'请填已汇总的最终 S、m；单个试件请使用三个试件原始值方式。'} 原始小数位保留。</p>${Array.from({length:specimens?3:1},(_,i)=>`<div class="bbr-specimen">${specimens?`<strong>试件 ${i+1}</strong>`:''}<div class="bbr-pair">${['S(60 s) · MPa','m(60 s) · 无量纲'].map((label,j)=>{const n=i*2+j;return `<label for="reading_${group.id}_${n}">${label}<input type="number" inputmode="decimal" min="0" step="any" id="reading_${group.id}_${n}" value="${esc(group.readings?.[n]??'')}" oninput="saveResultsDraft('${run.id}');updateGroupPreview('${run.id}','${group.id}')"></label>`}).join('')}</div></div>`).join('')}`;
}
function bbrCandidateSlot(candidate){
  const label=String(candidate.label||'').replace(/\s/g,''),times=[...label.matchAll(/(\d+(?:\.\d+)?)s/gi)].map(m=>Number(m[1]));
  for(const match of label.matchAll(/[Sm][(（](\d+(?:\.\d+)?)[)）]/gi))times.push(Number(match[1]));
  if(candidate.timeSeconds!==undefined&&candidate.timeSeconds!==null&&String(candidate.timeSeconds).trim()!=='')times.push(Number(candidate.timeSeconds));
  if(!times.length||times.some(t=>t!==60))return -1;
  const stripped=label.replace(/60(?:\.0+)?s/gi,'').replace(/([Sm])[(（]60(?:\.0+)?[)）]/gi,'$1').replace(/[()（）:：_\-]/g,'');
  if(/^(?:S|劲度|蠕变劲度|弯曲蠕变劲度|stiffness)(?:值|MPa|kPa)?$/i.test(stripped))return 0;
  if(/^(?:m|m值|蠕变速率|蠕变速率m|mvalue)$/i.test(stripped))return 1;
  return -1;
}
function bbrPhotoStiffnessUnit(unit){return /^mpa$/i.test(String(unit||'').trim())?'MPa':/^kpa$/i.test(String(unit||'').trim())?'kPa':''}
function bbrPhotoTarget(group){if(!bbrSpecimenMode(group))return 0;const n=[0,1,2].find(i=>!bbrValidPair((group.readings||[]).slice(i*2,i*2+2)));return n??0}
function editBBRPhotoTarget(gid,pid,aid,value){const {attempt}=photoAttempt(gid,pid,aid);if(attempt){attempt.bbrSpecimenIndex=Number(value);persistPhotos()}}
