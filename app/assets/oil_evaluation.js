let oilWorkspaceView='';
function ensureOilEvaluationData(){for(const key of ['yieldOilLibrary','yieldOilEvaluations','yieldOilMixtures'])if(!Array.isArray(db[key]))db[key]=[];if(!db.yieldOilDraft||typeof db.yieldOilDraft!=='object'||Array.isArray(db.yieldOilDraft))db.yieldOilDraft={}}
function oilPersist(){localStorage.setItem(KEY,JSON.stringify(db))}
function oilRemember(key,value,refresh=false){ensureOilEvaluationData();db.yieldOilDraft[key]=value;oilPersist();if(refresh)render()}
function oilNumber(value){return String(value??'').trim()===''?NaN:Number(value)}
function oilText(value){return String(value??'').trim()}
function oilLabel(id){const oil=db.yieldOilLibrary.find(o=>o.id===id);return oil?oil.name+(oil.batch?' / '+oil.batch:''):'未选择油品'}
function openOilWorkspace(view){ensureOilEvaluationData();oilWorkspaceView=view;render()}
function oilWorkspaceLinks(){return '<div class="card"><strong>原油评价与配比</strong><p class="muted">分别记录每种油的评价，再选择配比，最后保存混配实测结果。</p><div class="step-actions"><button onclick="openOilWorkspace(\'library\')">油品评价库</button><button class="secondary" onclick="openOilWorkspace(\'blend\')">配比参考</button></div></div>'}
function addEvaluationOil(){
  ensureOilEvaluationData();const d=db.yieldOilDraft,name=oilText(d.newOilName),batch=oilText(d.newOilBatch);if(!name)return alert('填写油品名称');
  if(db.yieldOilLibrary.some(o=>o.name===name&&o.batch===batch))return alert('此油品及批次已经存在');
  const oil={id:uid(),name,batch,created:Date.now()};db.yieldOilLibrary.push(oil);d.evalOil=oil.id;d.newOilName='';d.newOilBatch='';oilPersist();render();
}
function saveOilEvaluation(){
  ensureOilEvaluationData();const d=db.yieldOilDraft,oilId=d.evalOil,condition=oilText(d.evalCondition),yieldPct=oilNumber(d.evalYield),pen=oilNumber(d.evalPen);
  if(!db.yieldOilLibrary.some(o=>o.id===oilId)||!condition||!yieldValid(yieldPct,.01,100)||!yieldValid(pen,.01,1000))return alert('选择油品，填写评价条件、收率及针入度');
  if(db.yieldOilEvaluations.some(o=>o.oilId===oilId&&o.condition===condition&&o.yieldPct===yieldPct&&o.pen===pen))return alert('相同评价点已保存');
  db.yieldOilEvaluations.push({id:uid(),oilId,condition,yieldPct,pen,yieldBasis:'single-oil-feed',created:Date.now()});d.evalYield=d.evalPen='';oilPersist();render();
}
function oilRecipe(a,b,share,condition){
  if(!db.yieldOilLibrary.some(o=>o.id===a)||!condition||!yieldValid(share,.000001,100))return null;
  if(share===100)return {key:JSON.stringify([a,'',100,condition]),oilA:a,oilB:'',shareA:100,condition};
  if(a===b||!db.yieldOilLibrary.some(o=>o.id===b))return null;
  const [first,second,pct]=a<b?[a,b,share]:[b,a,100-share];
  return {key:JSON.stringify([first,second,Number(pct.toFixed(6)),condition]),oilA:a,oilB:b,shareA:share,condition};
}
function oilCurrentRecipe(){const d=db.yieldOilDraft;return oilRecipe(d.oilA,d.oilB,oilNumber(d.shareA??90),oilText(d.mixCondition))}
function saveOilMixture(){
  ensureOilEvaluationData();const d=db.yieldOilDraft,recipe=oilCurrentRecipe(),yieldPct=oilNumber(d.mixYield),pen=oilNumber(d.mixPen);
  if(!recipe||!yieldValid(yieldPct,.01,100)||!yieldValid(pen,.01,1000))return alert('选择原料油、质量配比和评价条件，填写混配后的实测收率、针入度');
  if(db.yieldOilMixtures.some(o=>o.recipeKey===recipe.key&&o.yieldPct===yieldPct&&o.pen===pen))return alert('此配方的相同实测点已保存');
  db.yieldOilMixtures.push({id:uid(),...recipe,recipeKey:recipe.key,yieldPct,pen,yieldBasis:'mixed-feed-total',created:Date.now()});d.mixYield=d.mixPen='';oilPersist();render();
}
function oilMixtureReferences(recipe){
  if(!recipe)return [];
  const rows=db.yieldOilMixtures.filter(o=>o.recipeKey===recipe.key);
  return recipe.shareA===100?[...rows,...db.yieldOilEvaluations.filter(o=>o.oilId===recipe.oilA&&o.condition===recipe.condition)]:rows;
}
function oilMeasuredCurve(rows){
  const points=[];for(const row of rows.slice().sort((a,b)=>a.yieldPct-b.yieldPct)){
    const last=points.at(-1);if(last&&Math.abs(last.y-row.yieldPct)<1e-8){if(Math.abs(last.p-row.pen)>1e-8)return {error:'同一收率有不同针入度，请先核对重复测定；暂不作插值。'};continue}points.push({y:row.yieldPct,p:row.pen});
  }
  if(points.length<2)return {error:'此配方尚不足两个实测收率点，可先查看原料评价并完成混配试验。'};
  if(points.some((p,i)=>i>0&&p.p<=points[i-1].p))return {error:'此配方实测趋势存在冲突，请核对条件，暂不作插值。'};
  return {points};
}
function oilRemoveRecord(collection,id){if(!['yieldOilEvaluations','yieldOilMixtures'].includes(collection)||!confirm('确定删除这条评价记录吗？'))return;db[collection]=db[collection].filter(o=>o.id!==id);oilPersist();render()}
function oilRecordsTable(rows,collection){return rows.length?`<div class="oil-data-scroll"><table class="oil-data-table"><thead><tr><th>评价条件</th><th>收率 %</th><th>针入度 0.1 mm</th><th></th></tr></thead><tbody>${rows.map(o=>`<tr><td>${esc(o.condition)}</td><td>${esc(o.yieldPct)}</td><td>${esc(o.pen)}</td><td><button class="secondary" onclick="oilRemoveRecord('${collection}','${o.id}')">删除</button></td></tr>`).join('')}</tbody></table></div>`:'<p class="muted">尚无对应评价记录。</p>'}
function oilWorkspaceUI(){
  ensureOilEvaluationData();const d=db.yieldOilDraft,v=key=>esc(d[key]??''),options=selected=>'<option value="">请选择油品</option>'+db.yieldOilLibrary.map(o=>`<option value="${o.id}" ${o.id===selected?'selected':''}>${esc(oilLabel(o.id))}</option>`).join('');
  const input=(key,label,type='text')=>`<label>${label}<input id="oil_${key}" type="${type}" ${type==='number'?'step="any"':''} value="${v(key)}" oninput="oilRemember('${key}',this.value)"></label>`;
  const header=`<div class="yield-page"><button class="secondary" onclick="openOilWorkspace('')">← 收率预测</button><h1>${oilWorkspaceView==='library'?'油品评价库':'配比参考'}</h1><div class="step-actions"><button class="secondary" onclick="openOilWorkspace('library')">油品评价库</button><button class="secondary" onclick="openOilWorkspace('blend')">配比参考</button></div>`;
  if(oilWorkspaceView==='library')return header+`<p class="muted">母油和调配油都先单独评价。每个油品批次可保存多个收率—针入度点；这里的收率以该油单独评价时的投料质量为分母。</p><div class="card"><h2>新增油品</h2><div class="yield-grid">${input('newOilName','油品名称')}${input('newOilBatch','批次／来源')}</div><button onclick="addEvaluationOil()">保存油品</button></div><div class="card"><h2>记录单油评价</h2><label>评价的油品<select onchange="oilRemember('evalOil',this.value)">${options(d.evalOil)}</select></label>${input('evalCondition','评价条件／方法（例如 25℃针入度／方案A）')}<div class="yield-grid">${input('evalYield','单油收率（%）','number')}${input('evalPen','针入度（0.1 mm）','number')}</div><button onclick="saveOilEvaluation()">保存评价点</button></div>${db.yieldOilLibrary.map(o=>`<details class="card"><summary>${esc(oilLabel(o.id))} · ${db.yieldOilEvaluations.filter(r=>r.oilId===o.id).length} 个评价点</summary>${oilRecordsTable(db.yieldOilEvaluations.filter(r=>r.oilId===o.id),'yieldOilEvaluations')}</details>`).join('')||'<p class="muted">先建立母油和调配油资料。</p>'}</div>`;
  const recipe=oilCurrentRecipe(),refs=oilMixtureReferences(recipe),condition=oilText(d.mixCondition),share=oilNumber(d.shareA??90),curve=oilMeasuredCurve(refs),target=oilNumber(d.targetPen);
  let prediction='';if(Number.isFinite(target)&&!curve.error){const y=yieldLocalInterpolate(curve.points,'p',target,'y');prediction=y===null?'<p class="muted">目标超出此配方实测范围，请补测。</p>':`<p>此配方内插值：目标 ${esc(target)} 针，对应收率约 <strong>${yieldFmt(y)}%</strong>。请实测验证。</p>`}
  const reference=(id,label)=>`<div class="card"><h3>${label}：${esc(oilLabel(id))}</h3>${oilRecordsTable(db.yieldOilEvaluations.filter(o=>o.oilId===id&&o.condition===condition),'yieldOilEvaluations')}</div>`;
  return header+`<div class="card"><h2>选择原料和配比</h2><label>油品 A（母油）<select onchange="oilRemember('oilA',this.value,true)">${options(d.oilA)}</select></label><label>油品 B（调配油）<select ${share===100?'disabled':''} onchange="oilRemember('oilB',this.value,true)">${options(d.oilB)}</select></label><label>A 的质量占比（%，100 表示纯 A）<input type="number" min="0.000001" max="100" step="any" list="oilCommonRatios" value="${esc(d.shareA??90)}" onchange="oilRemember('shareA',this.value,true)"><datalist id="oilCommonRatios"><option value="100"></option><option value="90"></option><option value="85"></option><option value="80"></option></datalist></label>${input('mixCondition','评价条件／方法（与单油评价使用相同名称）')}<button class="secondary" onclick="render()">查看此配比参考</button><p class="muted">质量比 A∶B = ${Number.isFinite(share)?esc(share):'—'}∶${Number.isFinite(share)?esc(100-share):'—'}；混配收率＝沥青质量÷总投料质量×100%。</p></div>${recipe?reference(recipe.oilA,'原料 A 单油评价')+(recipe.shareA===100?'':reference(recipe.oilB,'原料 B 单油评价')):'<p class="muted">请选原料、有效配比及评价条件。</p>'}<div class="card"><h2>此配方的混配实测</h2>${oilRecordsTable(refs.filter(o=>o.recipeKey),'yieldOilMixtures')}<p class="muted">${curve.error?esc(curve.error):'已有此配方的多个点，可在已测范围内反算目标针入度对应收率。'} 原料单油评价供配比试验参考，混配针入度需混配后实测，不直接按比例平均。</p>${input('targetPen','目标针入度（可选）','number')}<button class="secondary" onclick="render()">估算目标收率</button>${prediction}</div><div class="card"><h2>配比后再测并记录</h2><p class="muted">保存到当前油品、批次、比例和评价条件，不覆盖单油评价。</p><div class="yield-grid">${input('mixYield','混配实测收率（%）','number')}${input('mixPen','混配实测针入度（0.1 mm）','number')}</div><button onclick="saveOilMixture()">保存混配结果</button></div></div>`;
}
