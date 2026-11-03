function isMSCR(run){return run?.resultKind==='mscr'||/MSCR|多应力蠕变/i.test(String(run?.name||''))}
const MSCR_STANDARD='JTG 3410—2025 / T 0647';
function ensureMSCRTemplate(){
  let template=(db.templates||[]).find(t=>t.kind!=='heating'&&(t.resultKind==='mscr'||/^MSCR(?:试验|实验)?$/i.test(String(t.name||'').replace(/\s/g,''))));
  let changed=false;
  if(!template){template={id:uid(),resultKind:'mscr',name:'MSCR试验',standard:MSCR_STANDARD,unit:'Jnr: kPa⁻¹；Jnr diff: %',temps:'58,64,70,76,82',initialReadings:true,steps:[{name:'试样准备与恒温',minutes:0,mode:'none'},{name:'多应力蠕变恢复测定',minutes:0,mode:'none'},{name:'录入 Jnr 与 Jnr diff',minutes:0,mode:'none'}]};db.templates.push(template);changed=true}
  else if(template.resultKind!=='mscr'){template.resultKind='mscr';changed=true}
  if(template.unit==='Jnr: kPa⁻¹；R: %'){template.unit='Jnr: kPa⁻¹；Jnr diff: %';changed=true}
  for(const step of template.steps||[])if(step.name==='录入 Jnr 与恢复率'){step.name='录入 Jnr 与 Jnr diff';changed=true}
  for(const run of db.runs||[])if(isMSCR(run))for(const group of run.groups||[])if(syncMSCRDiff(group))changed=true;
  if(changed)localStorage.setItem(KEY,JSON.stringify(db));return template;
}
function mscrDiff(group){
  const raw=(group.readings||[]).slice(0,2);
  if(raw.length!==2||raw.some(v=>String(v??'').trim()===''||!Number.isFinite(Number(v))))return null;
  const [low,high]=raw.map(Number);if(low<=0||high<0)return null;
  const value=(high-low)/low*100;return Number.isFinite(value)?roundResult(value,.1).toFixed(1):null;
}
function syncMSCRDiff(group){
  const before=JSON.stringify(group);
  if(group.mscrThirdKind!=='jnrdiff'&&String(group.readings?.[2]??'').trim()!==''){
    group.mscrLegacyThirdValues=group.mscrLegacyThirdValues||[];
    group.mscrLegacyThirdValues.push({value:group.readings[2],stress:group.mscrRecoveryStress||'',label:'R35旧第三项（恢复率字段）'});
  }
  group.readings=group.readings||['','',''];while(group.readings.length<3)group.readings.push('');
  group.mscrThirdKind='jnrdiff';group.readings[2]=mscrDiff(group)??'';
  return before!==JSON.stringify(group);
}
function mscrValues(group){
  const diff=mscrDiff(group);return diff===null?null:[Number(group.readings[0]),Number(group.readings[1]),Number(diff)];
}
function mscrLabels(){return ['Jnr₀.₁ · 0.1 kPa 不可恢复蠕变柔量','Jnr₃.₂ · 3.2 kPa 不可恢复蠕变柔量','Jnr diff · 不可恢复蠕变柔量差异率']}
function mscrResultText(group){if(!mscrValues(group))return '—';const v=group.readings;return `Jnr₀.₁ = ${v[0]} kPa⁻¹；Jnr₃.₂ = ${v[1]} kPa⁻¹；Jnr diff = ${mscrDiff(group)} %`}
function mscrFieldsUI(run,group){
  const labels=mscrLabels(group);
  return `<p class="muted">三项独立显示，不求平均。Jnr diff =（Jnr₃.₂ − Jnr₀.₁）÷ Jnr₀.₁ × 100%，自动计算至 0.1%。Jnr₀.₁ 为 0 时无法计算；差异率可以大于 100%。</p><div class="mscr-fields">${labels.map((label,i)=>`<label for="reading_${group.id}_${i}">${esc(label)}（${i===2?'%':'kPa⁻¹'}）<input type="number" inputmode="decimal" ${i===2?'readonly aria-label="Jnr diff 自动计算"':'min="0"'} step="any" id="reading_${group.id}_${i}" value="${esc(i===2?mscrDiff(group)??'':group.readings?.[i]??'')}" ${i===2?'':`oninput="saveResultsDraft('${run.id}');updateGroupPreview('${run.id}','${group.id}')"`}></label>`).join('')}</div>${group.mscrLegacyThirdValues?.length?`<details><summary>R35旧第三项记录</summary>${group.mscrLegacyThirdValues.map(item=>`<p class="muted">${esc(item.label)}：${esc(item.value)} %${item.stress?'；应力 '+esc(item.stress)+' kPa':''}</p>`).join('')}</details>`:''}`;
}
