// Interpolate only measured cuts from the current feedstock batch and grade.
function yieldLocalCurve(grade,batch,pen,yieldPct){
  const selected=db.yieldObservations.filter(o=>yieldSelected.has(o.id));
  const records=selected.filter(o=>o.type==='cut'&&o.grade===grade&&String(o.batch||'').trim()===batch);
  const points=records.filter(o=>yieldValid(+o.yieldPct,.01,100)&&yieldValid(+o.pen,.01,1000)).map(o=>({y:+o.yieldPct,p:+o.pen}));
  points.push({y:yieldPct,p:pen});points.sort((a,b)=>a.y-b.y);
  const unique=[];
  for(const point of points){const last=unique.at(-1);if(last&&Math.abs(last.y-point.y)<1e-8){if(Math.abs(last.p-point.p)>1e-8)return {error:'本批同一收率对应不同针入度，请核对重复记录及本次初始点，确认后再计算。'};continue}unique.push(point)}
  for(let i=1;i<unique.length;i++)if(unique[i].p<=unique[i-1].p)return {error:'本批实测点未呈现收率增加、针入度增加的趋势，请核对母油批次和试验条件，暂不生成插值结果。'};
  if(unique.length<2)return {error:'本批至少需要两个不同收率的实测点（可包括本次初始点）。请保存并勾选同牌号、同母油批次的实测记录。'};
  return {points:unique,ignored:selected.length-records.length};
}
function yieldLocalInterpolate(points,input,value,output){
  if(!Number.isFinite(value)||!points?.length||value<points[0][input]-1e-9||value>points.at(-1)[input]+1e-9)return null;
  for(const p of points)if(Math.abs(p[input]-value)<1e-9)return p[output];
  for(let i=1;i<points.length;i++){const a=points[i-1],b=points[i];if(value>=a[input]&&value<=b[input])return a[output]+(value-a[input])*(b[output]-a[output])/(b[input]-a[input])}
  return null;
}
function calculateLocalYield(){
  const batch=yieldText('calcBatch'),p=yieldNum('calcPen'),y=yieldNum('calcYield'),target=yieldNum('calcTarget');
  if(!batch||!yieldValid(p,.01,1000)||!yieldValid(y,.01,100))return yieldError('填写母油批次、有效初始针入度及初始收率。');
  const curve=yieldLocalCurve(yieldGrade,batch,p,y);if(curve.error)return yieldError(curve.error);
  const first=curve.points[0],last=curve.points.at(-1),center=Number.isFinite(target)?yieldLocalInterpolate(curve.points,'p',target,'y'):null;
  if(Number.isFinite(target)&&center===null)return yieldError(`目标针入度超出本批已测 ${yieldFmt(first.p)}～${yieldFmt(last.p)} 范围，请补测相邻收率点；本次不向范围外推算。`);
  yieldSuggestedRange=[first.y,last.y];
  const prior=Number(yieldDraft.chosenYield),chosen=center??(yieldDraft.chosenYield!==''&&Number.isFinite(prior)&&prior>=first.y&&prior<=last.y?prior:y);
  get('yieldResult').innerHTML=`<div class="yield-result"><p><strong>${center===null?'本批实测曲线':`目标 ${esc(yieldFmt(target))} 针 · 估算收率 ${esc(yieldFmt(center))}%`}</strong></p><p>按 ${esc(yieldGrade)} / ${esc(batch)} 的 ${curve.points.length} 个点作区间内线性插值。可用收率 ${yieldFmt(first.y)}%～${yieldFmt(last.y)}%，对应针入度 ${yieldFmt(first.p)}～${yieldFmt(last.p)}。</p><p>${curve.points.map(o=>`${yieldFmt(o.y)}% → ${yieldFmt(o.p)} 针`).map(esc).join('；')}</p><label>选定切割收率（%）<input id="chosenYield" type="number" step="any" min="${first.y}" max="${last.y}" value="${esc(yieldFmt(chosen))}" oninput="rememberYieldInput(this);updateYieldChoice()"></label><div id="yieldChoice"></div><p class="muted">${curve.ignored?`已排除 ${curve.ignored} 条其他母油批次、其他牌号或掺蜡记录。`:''}请保持同批原料、相同针入度试验及取样条件；少量点仅提供局部估算，目标点仍需小样验证。本结果用于切割收率估算，不换算蜡油掺量。</p></div>`;
  updateLocalYieldChoice();
}
function updateLocalYieldChoice(){
  const panel=get('yieldChoice');if(!panel)return;
  const curve=yieldLocalCurve(yieldGrade,yieldText('calcBatch'),yieldNum('calcPen'),yieldNum('calcYield'));
  if(curve.error){panel.textContent=curve.error;return}
  const chosen=yieldNum('chosenYield'),prediction=yieldLocalInterpolate(curve.points,'y',chosen,'p');
  panel.innerHTML=prediction===null?'<p class="muted">选定收率超出本批实测范围，请补测后再估算。</p>':`<p>本批插值针入度约 <strong>${yieldFmt(prediction)}</strong>（0.1 mm）。</p>`;
}
