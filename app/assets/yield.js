// Yield is expressed in percentage points of original crude. Predictions are planning estimates.
if(!Array.isArray(db.yieldObservations))db.yieldObservations=[];
if(!Array.isArray(db.yieldGrades))db.yieldGrades=[];
if(!db.yieldDrafts||typeof db.yieldDrafts!=='object'||Array.isArray(db.yieldDrafts))db.yieldDrafts={};
let yieldGrade='', yieldDraft={}, yieldSelected=new Set(), yieldIncludeOther=false, yieldMenuGrade=null, yieldSuggestedRange=null;
const yieldNum=id=>{const v=get(id)?.value?.trim();return v===''?NaN:Number(v)};
const yieldText=id=>get(id)?.value?.trim()||'';
const yieldValid=(v,min,max)=>Number.isFinite(v)&&v>=min&&v<=max;
const yieldId=x=>encodeURIComponent(x).replace(/'/g,'%27');
const yieldFmt=v=>Number(v).toFixed(2).replace(/\.?0+$/,'');
const yieldError=s=>{get('yieldResult').innerHTML=`<p class="red">${s}</p>`};
const yieldUsableObservation=o=>o.type==='cut'||(o.type==='wax'&&o.yieldChangePct!==undefined&&yieldValid(+o.initialYield,0.01,100)&&yieldValid(+o.yieldChangePct,0.01,100)&&+o.initialYield*(1+(+o.yieldChangePct)/100)<=100&&yieldValid(+o.basePen,20,80)&&yieldValid(+o.finalPen,20,80)&&+o.finalPen>+o.basePen);

function yieldUI(){
  const grades=[...new Set([...db.yieldGrades,...db.yieldObservations.map(o=>o.grade)].filter(Boolean))];
  if(!yieldGrade)return `<div class="yield-page"><h1>收率预测</h1><p class="muted">进入牌号后可选原经验公式或新版实测校准算法。</p><div class="card"><label>新增沥青牌号<input id="newYieldGrade" value="${esc(db.yieldNewGrade||'')}" oninput="rememberNewYieldGrade(this.value)" placeholder="例如 1124"></label><button onclick="addYieldGrade()">建立牌号卡片</button></div><div class="yield-cards">${grades.map(g=>`<div class="card yield-grade-card"><button class="yield-grade-open" onclick="openYieldGrade('${yieldId(g)}')" title="${esc(g)}"><span class="yield-grade-name">${esc(g)}</span></button><div class="yield-grade-menu"><button class="more-btn" aria-label="${esc(g)} 牌号选项" onclick="toggleYieldGradeMenu('${yieldId(g)}')">⋮</button>${yieldMenuGrade===g?`<div class="run-menu"><button class="danger" onclick="deleteYieldGrade('${yieldId(g)}')">删除牌号</button></div>`:''}</div></div>`).join('')}</div></div>`;
  const all=db.yieldObservations.filter(o=>(o.type==='cut'||o.type==='wax')&&(o.grade===yieldGrade||yieldIncludeOther));
  const records=all.map(o=>`<div class="yield-record"><label class="yield-select"><input type="checkbox" ${yieldUsableObservation(o)?'':'disabled'} ${yieldSelected.has(o.id)?'checked':''} onchange="toggleYieldRecord('${o.id}',this.checked)"><span><strong>${esc(o.grade)} · ${esc(o.batch)}</strong><small>${o.type==='wax'?`掺蜡：${o.yieldChangePct===undefined?`旧记录 · 质量比 ${yieldFmt(o.waxPct)}%`:`初始收率 ${yieldFmt(o.initialYield)}% · 收率变化率 ${yieldFmt(o.yieldChangePct)}%`} · 针入度 ${yieldFmt(o.basePen)}→${yieldFmt(o.finalPen)}${yieldUsableObservation(o)?'':' · 仅存档'}`:`收率 ${yieldFmt(o.yieldPct)}% → 针入度 ${yieldFmt(o.pen)}（0.1 mm）`}</small></span></label><button class="secondary" onclick="deleteYieldObservation('${o.id}')">删除</button></div>`).join('');
  const v=id=>esc(yieldDraft[id]??'');
  return `<div class="yield-page"><button class="secondary" onclick="backYieldGrade()">← 牌号卡片</button><h1>${esc(yieldGrade)} · 收率计算</h1><p class="muted">可在原经验公式与新版实测校准之间切换。实测校准使用下方勾选的记录；结果供小样调和验证。</p><div class="card compact-form"><label>预测算法</label><input hidden id="calcMode" value="${yieldDraft.calcMode==='experience'?'experience':'calibrated'}"><div class="yield-mode-switch" role="group" aria-label="选择预测算法"><button class="${yieldDraft.calcMode==='experience'?'active':''}" aria-pressed="${yieldDraft.calcMode==='experience'}" onclick="setYieldMode('experience')">原经验公式</button><button class="${yieldDraft.calcMode!=='experience'?'active':''}" aria-pressed="${yieldDraft.calcMode!=='experience'}" onclick="setYieldMode('calibrated')">新版实测校准</button></div><p class="yield-mode-caption">${yieldDraft.calcMode==='experience'?'按原分段经验预测，保留不同同事经验的范围。':'结合牌号、批次和所选实测修正；没有有效实测时使用经验基线。'}</p><div class="yield-grid"><label>本次批次<input id="calcBatch" oninput="rememberYieldInput(this)" value="${v('calcBatch')}" placeholder="例如 20260929-A"></label><label>初始针入度（0.1 mm）<input id="calcPen" oninput="rememberYieldInput(this)" type="number" step="any" value="${v('calcPen')}"></label><label>初始收率（%）<input id="calcYield" oninput="rememberYieldInput(this)" type="number" step="any" value="${v('calcYield')}"></label><label>当前沥青质量（g）<input id="calcMass" oninput="rememberYieldInput(this)" type="number" step="any" value="${v('calcMass')}"></label><label>目标针入度（0.1 mm，可留空）<input id="calcTarget" oninput="rememberYieldInput(this)" type="number" step="any" value="${v('calcTarget')}"></label></div><div class="step-actions"><button onclick="calculateYield()">估算收率范围</button></div><div id="yieldResult" aria-live="polite"></div></div><h2>参考实测记录</h2><div class="card compact-form"><div class="step-actions"><button class="secondary" onclick="selectYieldRecords(true)">全选</button><button class="secondary" onclick="selectYieldRecords(false)">清空选择</button></div><label class="yield-select"><input type="checkbox" ${yieldIncludeOther?'checked':''} onchange="yieldIncludeOther=this.checked;captureYieldDraft();render()">允许选择其他牌号的数据</label>${records||'<p class="muted">尚无实测记录；仍可按分段经验估算。</p>'}</div><h2>新增实测点</h2><div class="card compact-form"><p class="muted">每次记录独立保存；同一批次多个收率点可形成校准曲线。保存后自行勾选参与计算。</p><div class="yield-grid"><label>收率（%）<input id="obsYield" type="number" step="any" value="${v('obsYield')}" oninput="rememberYieldInput(this)"></label><label>针入度（0.1 mm）<input id="obsPen" type="number" step="any" value="${v('obsPen')}" oninput="rememberYieldInput(this)"></label></div><button onclick="saveYieldObservation('cut')">保存本次实测</button></div><details><summary>蜡油掺量记录</summary><div class="card compact-form"><p class="muted">使用本次批次和上方初始收率，记录收率相对变化的百分比。保存后可勾选作为经验修正，请用小样复核。</p><div class="yield-grid"><label>掺前针入度<input id="obsBasePen" type="number" step="any" value="${v('obsBasePen')}" oninput="rememberYieldInput(this)"></label><label>收率变化率（相对初始收率，%）<input id="obsYieldChangePct" type="number" step="any" value="${v('obsYieldChangePct')}" oninput="rememberYieldInput(this)"></label><label>掺后针入度<input id="obsFinalPen" type="number" step="any" value="${v('obsFinalPen')}" oninput="rememberYieldInput(this)"></label></div><button class="secondary" onclick="saveYieldObservation('wax')">保存掺蜡实测</button></div></details></div>`;
}
function setYieldMode(mode){captureYieldDraft();yieldDraft.calcMode=mode;persistYieldDraft();render();refreshYieldPrediction()}
function refreshYieldPrediction(){if(get('yieldResult')&&yieldText('calcBatch')&&yieldValid(yieldNum('calcPen'),20,80)&&yieldValid(yieldNum('calcYield'),0.01,100))calculateYield()}
function persistYieldDraft(){if(!yieldGrade)return;db.yieldDrafts[yieldGrade]={...yieldDraft};localStorage.setItem(KEY,JSON.stringify(db))}
function rememberYieldInput(input){yieldDraft[input.id]=input.value;persistYieldDraft()}
function rememberNewYieldGrade(value){db.yieldNewGrade=value;localStorage.setItem(KEY,JSON.stringify(db))}
function captureYieldDraft(){for(const id of ['calcBatch','calcPen','calcYield','calcMass','calcTarget','calcMode','obsYield','obsPen','obsBasePen','obsYieldChangePct','obsFinalPen','chosenYield'])if(get(id))yieldDraft[id]=get(id).value;persistYieldDraft()}
function addYieldGrade(){const g=yieldText('newYieldGrade');if(!g)return alert('填写牌号');if(!db.yieldGrades.includes(g))db.yieldGrades.push(g);db.yieldNewGrade='';yieldGrade=g;yieldDraft={...(db.yieldDrafts[g]||{})};yieldSelected=new Set();yieldMenuGrade=null;save()}
function openYieldGrade(encoded){yieldGrade=decodeURIComponent(encoded);yieldDraft={...(db.yieldDrafts[yieldGrade]||{})};yieldIncludeOther=false;yieldSelected=new Set(db.yieldObservations.filter(o=>o.grade===yieldGrade&&yieldUsableObservation(o)).map(o=>o.id));yieldMenuGrade=null;render();refreshYieldPrediction()}
function toggleYieldGradeMenu(encoded){const g=decodeURIComponent(encoded);yieldMenuGrade=yieldMenuGrade===g?null:g;render()}
function deleteYieldGrade(encoded){const g=decodeURIComponent(encoded),records=db.yieldObservations.filter(o=>o.grade===g).length;
  if(!confirm(`确定删除牌号“${g}”及其 ${records} 条实测记录吗？此操作无法撤销。`))return;
  db.yieldGrades=db.yieldGrades.filter(x=>x!==g);db.yieldObservations=db.yieldObservations.filter(o=>o.grade!==g);
  delete db.yieldDrafts[g];if(yieldGrade===g){yieldGrade='';yieldDraft={}}yieldSelected=new Set([...yieldSelected].filter(id=>db.yieldObservations.some(o=>o.id===id)));
  yieldMenuGrade=null;save();
}
function toggleYieldRecord(id,checked){checked?yieldSelected.add(id):yieldSelected.delete(id);refreshYieldPrediction()}
function selectYieldRecords(checked){captureYieldDraft();yieldSelected=new Set(checked?db.yieldObservations.filter(o=>(o.grade===yieldGrade||yieldIncludeOther)&&yieldUsableObservation(o)).map(o=>o.id):[]);render();refreshYieldPrediction()}

// The file covers 30-70. Extend only one neighboring interval to 20-30 and
// 70-80 using its nearest slope, and label these portions as extrapolation.
const yieldEdges=[20,30,40,50,60,70,80],yieldSlopes=[6,6,8,10,12,12];
function piecewiseYield(start,target){
  if(start<20||target>80||target<=start)return null;
  let current=start,total=0;
  for(let i=0;i<yieldSlopes.length&&current<target;i++){
    if(current>=yieldEdges[i+1])continue;
    const end=Math.min(target,yieldEdges[i+1]);
    if(end>current){total+=(end-current)/yieldSlopes[i];current=end}
  }
  return Math.abs(current-target)<1e-9?total:null;
}
function piecewisePen(start,delta){
  if(start<20||start>80||delta<0)return null;
  let p=start,left=delta;
  for(let i=0;i<yieldSlopes.length&&left>1e-9;i++){
    const edge=yieldEdges[i+1];if(p>=edge)continue;
    const used=Math.min(left,(edge-p)/yieldSlopes[i]);p+=used*yieldSlopes[i];left-=used;
  }
  return left<=1e-9?Math.min(80,p):null;
}
// Calibrate only from within-batch changes. Absolute points from different batches have different offsets.
function yieldRatios(grade,batch,pen,yieldPct){
  const selected=db.yieldObservations.filter(o=>yieldSelected.has(o.id));
  const byBatch=new Map();for(const o of selected){const key=o.grade+'\u0000'+o.batch;if(!byBatch.has(key))byBatch.set(key,[]);byBatch.get(key).push(o)}
  const entries=[];let conflicts=0,unusable=0;
  for(const [key,group] of byBatch){
    const points=group.filter(o=>o.type==='cut'&&yieldValid(+o.yieldPct,0.01,100)&&yieldValid(+o.pen,0.01,1000)).map(o=>({y:+o.yieldPct,p:+o.pen}));
    if(key===grade+'\u0000'+batch&&!points.some(o=>Math.abs(o.y-yieldPct)<1e-9&&Math.abs(o.p-pen)<1e-9))points.push({y:yieldPct,p:pen});
    points.sort((a,b)=>a.y-b.y);
    for(let i=1;i<points.length;i++){
      const a=points[i-1],b=points[i],baseline=piecewiseYield(a.p,b.p),dy=b.y-a.y;
      if(dy<=0||b.p<=a.p||baseline===null){conflicts++;continue}
      const ratio=baseline/dy;if(ratio>=0.4&&ratio<=2.5)entries.push({ratio,grade:group[0].grade,batch:group[0].batch,type:'cut',mid:(a.p+b.p)/2});else conflicts++;
    }
    for(const o of group.filter(o=>o.type==='wax')){
      // Recorded yield change is relative to the starting yield.
      // Convert relative percent to yield percentage points before comparing slopes.
      const initial=+o.initialYield,delta=initial*(+o.yieldChangePct)/100,baseline=piecewiseYield(+o.basePen,+o.finalPen);
      if(!yieldValid(initial,0.01,100)||o.yieldChangePct===undefined||!yieldValid(+o.yieldChangePct,0.01,100)||initial+delta>100||baseline===null||delta<=0){unusable++;continue}
      const ratio=baseline/delta;if(ratio>=0.4&&ratio<=2.5)entries.push({ratio,grade:o.grade,batch:o.batch,type:'wax',mid:(+o.basePen+(+o.finalPen))/2});else conflicts++;
    }
  }
  return {entries,ratios:entries.map(e=>e.ratio),conflicts,unusable,count:selected.length};
}
function yieldCalibration(model,grade,batch,mid){
  const rows=(model.entries||[]).map(e=>{
    // Same batch, nearby penetration, and direct cut observations are more transferable.
    const weight=(e.grade===grade?1:0.25)*(e.batch===batch&&e.grade===grade?1.5:0.7)*(e.type==='wax'?0.7:1)/(1+Math.abs(e.mid-mid)/20);
    return {...e,weight};
  }).filter(e=>e.weight>0).sort((a,b)=>a.ratio-b.ratio);
  if(!rows.length)return {factor:1,raw:1,strength:0,used:0};
  const total=rows.reduce((sum,e)=>sum+e.weight,0);
  let accumulated=0,raw=rows[rows.length-1].ratio;
  for(const e of rows){accumulated+=e.weight;if(accumulated>=total/2){raw=e.ratio;break}}
  // Shrink sparse local observations toward the lab's prior rather than fully replacing it.
  const strength=total/(total+2),factor=Math.exp(strength*Math.log(raw));
  return {factor,raw,strength,used:rows.length};
}
function yieldRange(prior,delta,model,grade=yieldGrade,batch='',mid=50){
  // Other daily reports give 8 and 10 penetration points per percentage point.
  const candidates=[prior,delta/8,delta/10];
  const factor=yieldCalibration(model,grade,batch,mid).factor;
  const calibrated=candidates.map(x=>x/factor);
  return [Math.min(...calibrated),Math.max(...calibrated)];
}
function waxMassForYield(asphaltMass,initialYield,predictedYield){
  if(!yieldValid(asphaltMass,0.001,1e9)||!yieldValid(initialYield,0.01,100)||!yieldValid(predictedYield,initialYield,100))return null;
  const crudeMass=asphaltMass/(initialYield/100);
  return crudeMass*((predictedYield-initialYield)/100);
}
function calculateYield(){
  yieldSuggestedRange=null;
  captureYieldDraft();const batch=yieldText('calcBatch'),p=yieldNum('calcPen'),y=yieldNum('calcYield'),mass=yieldNum('calcMass'),target=yieldNum('calcTarget');
  if(!batch||!yieldValid(p,20,80)||!yieldValid(y,0.01,100))return yieldError('填写批次、20～80 范围内的初始针入度及有效初始收率。');
  const mode=yieldText('calcMode')||yieldDraft.calcMode||'calibrated';
  const model=mode==='experience'?{entries:[],ratios:[],count:0,conflicts:0,unusable:0}:yieldRatios(yieldGrade,batch,p,y), notes=[];
  if(p<30||Number.isFinite(target)&&target>70)notes.push('30～70 之外是按最邻近区间斜率延伸的外推值，文件没有该区间的实测经验表。');
  if(model.conflicts)notes.push(`${model.conflicts} 组实测趋势冲突、超出分段范围或重复，未参与校准。`);
  if(model.unusable)notes.push(`${model.unusable} 条掺蜡记录为旧质量比口径或数值无效，未参与校准。`);
  if(model.count&&!model.ratios.length)notes.push('所选记录尚未形成有效的收率变化对照：切割记录需要两个不同收率点（可包括本次初始点）；掺蜡记录需要保存时的初始收率。当前使用分段经验。');
  if(model.ratios.length===1)notes.push('目前只有一组有效变化，已适度降低该记录对结果的影响；请用小样复核。');
  const modelText=mode==='experience'?'原经验公式（未使用实测）':model.ratios.length?`新版算法 · 参考 ${model.ratios.length} 组有效变化校准`:'新版算法 · 暂无有效实测，按原经验公式估算';
  if(Number.isFinite(target)&&target>p){
    const prior=piecewiseYield(p,target);if(prior===null)return yieldError('目标针入度须高于初始值且不超过 80；20～30 和 70～80 属于外推。');
    const calibration=yieldCalibration(model,yieldGrade,batch,(p+target)/2);
    const [lo,hi]=yieldRange(prior,target-p,model,yieldGrade,batch,(p+target)/2).map(d=>y+d);
    if(hi>100)return yieldError('建议收率超过 100%，请检查输入或目标。');
    yieldSuggestedRange=[lo,hi];
    const waxLo=waxMassForYield(mass,y,lo),waxHi=waxMassForYield(mass,y,hi);
    const waxRange=waxLo===null?'填入当前沥青质量后显示对应的加蜡油质量范围。':`<strong>预计加蜡油 ${yieldFmt(waxLo)}～${yieldFmt(waxHi)} g</strong>（收率增量分别为 ${yieldFmt(lo-y)}～${yieldFmt(hi-y)} 个百分点）。`;
    const priorChoice=Number(yieldDraft.chosenYield),chosenChoice=yieldDraft.chosenYield!==''&&priorChoice>=lo&&priorChoice<=hi?priorChoice:y+prior/calibration.factor;
    get('yieldResult').innerHTML=`<div class="yield-result"><p><strong>建议收率 ${yieldFmt(lo)}%～${yieldFmt(hi)}%</strong> · ${modelText}</p><p>${waxRange}</p><p>计算：当前沥青质量 ÷（初始收率 ÷ 100）×［（预测收率 − 初始收率）÷ 100］。</p><p>未校准的分段经验参考值 ${yieldFmt(y+prior)}%；${model.ratios.length?`实测修正系数 ${yieldFmt(calibration.factor)}（原始 ${yieldFmt(calibration.raw)}，少量数据已向经验值收缩），校准中心约 ${yieldFmt(y+prior/calibration.factor)}%。`:'当前无有效校准。'} 建议范围保留不同日报经验口径的差异，不代表统计置信区间。</p><label>选定收率（%）<input id="chosenYield" type="number" step="any" min="${yieldFmt(lo)}" max="${yieldFmt(hi)}" value="${esc(yieldFmt(chosenChoice))}" oninput="rememberYieldInput(this);updateYieldChoice()"></label><div id="yieldChoice"></div><p class="muted">${notes.map(esc).join(' ')}增量按原油质量口径；最终针入度需实测确认。</p></div>`;updateYieldChoice();
  }else if(Number.isFinite(target)&&target<=p){yieldError('目标针入度应高于初始值；这里不预测降低针入度的调和。')}
  else{
    yieldSuggestedRange=null;
    get('yieldResult').innerHTML=`<div class="yield-result"><p>填写目标针入度可得到建议收率范围；也可直接输入选定收率预测针入度。</p><label>选定收率（%）<input id="chosenYield" type="number" step="any" min="${yieldFmt(y)}" max="100" value="${esc(yieldDraft.chosenYield||'')}" oninput="rememberYieldInput(this);updateYieldChoice()"></label><div id="yieldChoice"></div><p class="muted">${modelText}。${notes.map(esc).join(' ')}</p></div>`;
  }
}
function updateYieldChoice(){
  const chosen=yieldNum('chosenYield'),y=yieldNum('calcYield'),p=yieldNum('calcPen'),mass=yieldNum('calcMass');
  if(!Number.isFinite(chosen)||chosen<=y||chosen>100){get('yieldChoice').innerHTML='<p class="muted">选定收率须高于初始收率且不超过 100%。</p>';return}
  if(yieldSuggestedRange&&(chosen<yieldSuggestedRange[0]-0.0051||chosen>yieldSuggestedRange[1]+0.0051)){get('yieldChoice').innerHTML=`<p class="muted">请在 ${yieldFmt(yieldSuggestedRange[0])}%～${yieldFmt(yieldSuggestedRange[1])}% 的建议范围内选择。</p>`;return}
  const mode=yieldText('calcMode')||yieldDraft.calcMode||'calibrated';
  const model=mode==='experience'?{entries:[],ratios:[]}:yieldRatios(yieldGrade,yieldText('calcBatch'),p,y),base=piecewisePen(p,chosen-y);
  let prediction=base===null?'超出 20～80 的估算范围':`经验中心约 ${yieldFmt(base)}（0.1 mm）${p<30||base>70?'（含相邻区间外推）':''}`;
  if(model.ratios.length&&base!==null){const factor=yieldCalibration(model,yieldGrade,yieldText('calcBatch'),(p+base)/2).factor;const adjusted=piecewisePen(p,(chosen-y)*factor);if(adjusted!==null)prediction+=`；实测校准约 ${yieldFmt(adjusted)}`}
  const chosenWax=waxMassForYield(mass,y,chosen);
  const wax=chosenWax!==null?`按选定收率预计加蜡油 <strong>${yieldFmt(chosenWax)} g</strong>`:'填入当前沥青质量后计算加蜡克数';
  get('yieldChoice').innerHTML=`<p>${wax}</p><p>对应针入度：${prediction}。</p>`;
}
function saveYieldObservation(type){
  captureYieldDraft();const batch=yieldText('calcBatch'),id=uid();if(!batch)return alert('先填写本次批次');
  if(type==='wax'){
    const basePen=yieldNum('obsBasePen'),yieldChangePct=yieldNum('obsYieldChangePct'),finalPen=yieldNum('obsFinalPen'),initialYield=yieldNum('calcYield');
    if(!yieldValid(initialYield,0.01,100)||!yieldValid(basePen,20,80)||!yieldValid(yieldChangePct,0.01,100)||initialYield*(1+yieldChangePct/100)>100||!yieldValid(finalPen,20,80)||finalPen<=basePen)return alert('填写初始收率、20～80 范围内掺前和掺后针入度，以及有效收率变化率；掺后针入度应更高且调整后收率不超过 100%。');
    db.yieldObservations.push({id,type,grade:yieldGrade,batch,initialYield,basePen,yieldChangePct,finalPen,created:Date.now()});yieldSelected.add(id);
  }else{
    const yieldPct=yieldNum('obsYield'),pen=yieldNum('obsPen');
    if(!yieldValid(yieldPct,0.01,100)||!yieldValid(pen,0.01,1000))return alert('填写有效收率和针入度');
    db.yieldObservations.push({id,type:'cut',grade:yieldGrade,batch,yieldPct,pen,created:Date.now()});yieldSelected.add(id);
  }
  save();refreshYieldPrediction();
}
function deleteYieldObservation(id){if(!confirm('删除这条实测记录吗？'))return;db.yieldObservations=db.yieldObservations.filter(o=>o.id!==id);yieldSelected.delete(id);captureYieldDraft();save();refreshYieldPrediction()}

// Handle the prediction detail before the global navigation returns to the home screen.
function backYieldGrade(){
  if(!yieldGrade)return false;
  captureYieldDraft();yieldGrade='';yieldMenuGrade=null;yieldSuggestedRange=null;render();return true;
};
