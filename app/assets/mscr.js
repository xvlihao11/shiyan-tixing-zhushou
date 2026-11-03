function isMSCR(run){return run?.resultKind==='mscr'||/MSCR|多应力蠕变/i.test(String(run?.name||''))}
const MSCR_STANDARD='JTG 3410—2025 / T 0647';
function ensureMSCRTemplate(){
  let template=(db.templates||[]).find(t=>t.kind!=='heating'&&(t.resultKind==='mscr'||/^MSCR(?:试验|实验)?$/i.test(String(t.name||'').replace(/\s/g,''))));
  let changed=false;
  if(!template){template={id:uid(),resultKind:'mscr',name:'MSCR试验',standard:MSCR_STANDARD,unit:'Jnr: kPa⁻¹；R: %',temps:'58,64,70,76,82',initialReadings:true,steps:[{name:'试样准备与恒温',minutes:0,mode:'none'},{name:'多应力蠕变恢复测定',minutes:0,mode:'none'},{name:'录入 Jnr 与恢复率',minutes:0,mode:'none'}]};db.templates.push(template);changed=true}
  else if(template.resultKind!=='mscr'){template.resultKind='mscr';changed=true}
  if(changed)localStorage.setItem(KEY,JSON.stringify(db));return template;
}
function mscrRecoveryStress(group){return ['0.1','3.2'].includes(group.mscrRecoveryStress)?group.mscrRecoveryStress:''}
function mscrValues(group){
  const raw=group.readings||[],stress=mscrRecoveryStress(group);
  if(raw.length!==3||!stress||raw.some(v=>String(v??'').trim()===''||!Number.isFinite(Number(v))))return null;
  const values=raw.map(Number);if(values[0]<0||values[1]<0||values[2]<0||values[2]>100)return null;
  return values;
}
function mscrLabels(group){return ['Jnr₀.₁ · 0.1 kPa 不可恢复蠕变柔量','Jnr₃.₂ · 3.2 kPa 不可恢复蠕变柔量','R · '+(mscrRecoveryStress(group)?mscrRecoveryStress(group)+' kPa ':'应力未选择 · ')+'蠕变恢复率']}
function mscrResultText(group){if(!mscrValues(group))return '—';const v=group.readings;return `Jnr₀.₁ = ${v[0]} kPa⁻¹；Jnr₃.₂ = ${v[1]} kPa⁻¹；R(${mscrRecoveryStress(group)} kPa) = ${v[2]} %`}
function setMSCRRecoveryStress(rid,gid,stress){const run=db.runs.find(r=>r.id===rid),group=run?.groups.find(g=>g.id===gid);if(!group||!['','0.1','3.2'].includes(stress))return;collectResults(run);group.mscrRecoveryStress=stress;saveResultsDraft(rid);updateGroupPreview(rid,gid);render()}
function mscrFieldsUI(run,group){
  const labels=mscrLabels(group);
  return `<p class="muted">三项是不同指标，分别保存，不求平均。Jnr 单位为 kPa⁻¹；R 单位为 %，请注明对应应力。填写仪器已汇总的结果。</p><label>恢复率对应的应力<select aria-label="MSCR 恢复率应力" onchange="setMSCRRecoveryStress('${run.id}','${group.id}',this.value)"><option value="">请选择 R 对应的应力</option>${['3.2','0.1'].map(stress=>`<option value="${stress}" ${mscrRecoveryStress(group)===stress?'selected':''}>${stress} kPa</option>`).join('')}</select></label><div class="mscr-fields">${labels.map((label,i)=>`<label for="reading_${group.id}_${i}">${esc(label)}（${i===2?'%':'kPa⁻¹'}）<input type="number" inputmode="decimal" min="0" ${i===2?'max="100"':''} step="any" id="reading_${group.id}_${i}" value="${esc(group.readings?.[i]??'')}" oninput="saveResultsDraft('${run.id}');updateGroupPreview('${run.id}','${group.id}')"></label>`).join('')}</div>`;
}
