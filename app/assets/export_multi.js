// Independent selection of experiments across any combination of dates and samples.
const exportSelectedRuns=new Set();
function exportableRun(r){return !isHeating(r)&&r.groups?.some(g=>g.readings?.some(v=>String(v).trim()!==''&&Number.isFinite(Number(v))))}
function exportableRuns(){return db.runs.filter(exportableRun)}

exportUI=function(){
  const dates=[...new Set(db.samples.map(s=>s.date||localDateKey(s.created)))].sort().reverse();
  const dateCards=dates.map(day=>{
    const samples=db.samples.filter(s=>(s.date||localDateKey(s.created))===day);
    const sampleCards=samples.map(sample=>{
      const runs=exportableRuns().filter(r=>r.sampleId===sample.id);
      if(!runs.length)return '';
      return `<details class="export-sample"><summary><span class="export-heading"><span class="export-name">${esc(sample.name)}</span><small>${runs.length} 项</small></span><input type="checkbox" aria-label="选择 ${esc(sample.name)} 的全部实验" data-export-sample="${esc(sample.id)}" onclick="event.stopPropagation()" onchange="changeExportScope('sample',this.dataset.exportSample,this.checked)"></summary><div class="export-runs">${runs.map(r=>`<label class="export-run"><span>${r.phase&&r.phase!=='原样'?esc(r.phase)+' · ':''}${esc(r.name)}${isHeating(r)?' · 计时':''}</span><input type="checkbox" data-export-run="${esc(r.id)}" ${exportSelectedRuns.has(r.id)?'checked':''} onchange="changeExportScope('run',this.dataset.exportRun,this.checked)"></label>`).join('')}</div></details>`;
    }).join('');
    if(!sampleCards)return '';
    const count=exportableRuns().filter(r=>samples.some(s=>s.id===r.sampleId)).length;
    return `<details class="export-date"><summary><span class="export-heading"><strong>${esc(day)}</strong><small>${count} 项实验</small></span><input type="checkbox" aria-label="选择 ${esc(day)} 的全部实验" data-export-date="${esc(day)}" onclick="event.stopPropagation()" onchange="changeExportScope('date',this.dataset.exportDate,this.checked)"></summary><div class="export-samples">${sampleCards}</div></details>`;
  }).filter(Boolean);
  const older=dateCards.slice(3);
  const archive=older.length?`<details class="export-date export-archive"><summary><span class="export-heading"><strong>往期记录</strong><small>${older.length} 个日期</small></span></summary><div class="export-archive-dates">${older.join('')}</div></details>`:'';
  const tree=dateCards.slice(0,3).join('')+archive;
  return `<div class="export-page"><h1>导出实验数据</h1><p class="muted export-help">仅显示已录入读数的实验。展开日期和样品，可跨日期勾选；右侧方框可选中该层全部实验。</p><div class="export-panel"><div class="export-toolbar"><span id="exportCount">已选择 0 项实验</span><button class="secondary" onclick="changeExportScope('all','',true)">全选</button><button class="secondary" onclick="changeExportScope('all','',false)">清空</button></div><div class="export-tree">${tree||'<p class="muted export-empty">还没有可导出的实验。</p>'}</div><label class="export-option" for="includeExportDetails"><span>添加实验条件与步骤时间</span><input id="includeExportDetails" type="checkbox"></label><button class="export-submit" onclick="exportMarkdown('html')">导出横向报告</button><button class="secondary" onclick="exportMarkdown('md')">导出 Markdown .md</button></div>${fullBackupUI()}<details class="export-backup"><summary>旧版 JSON 数据备份</summary><div><button class="secondary" onclick="backupJSON()">导出 JSON（不含照片与文件）</button><p id="textExportStatus" role="status">${typeof textExportStatus==='string'?esc(textExportStatus):''}</p><label for="restoreExportBackup">恢复备份</label><input id="restoreExportBackup" type="file" accept="application/json,.json" onchange="restoreJSON(event)"><small class="muted">恢复会替换本机当前数据，请先备份。</small></div></details></div>`;
};

function changeExportScope(scope,id,checked){
  let runs=scope==='all'?exportableRuns():scope==='run'?exportableRuns().filter(r=>r.id===id):scope==='sample'?exportableRuns().filter(r=>r.sampleId===id):exportableRuns().filter(r=>{
    let s=db.samples.find(s=>s.id===r.sampleId);return s&&(s.date||localDateKey(s.created))===id;
  });
  for(const run of runs)checked?exportSelectedRuns.add(run.id):exportSelectedRuns.delete(run.id);
  syncExportSelection();
}

function syncExportSelection(){
  if(page!=='export')return;
  const existing=new Set(exportableRuns().map(r=>r.id));
  for(const id of exportSelectedRuns)if(!existing.has(id))exportSelectedRuns.delete(id);
  document.querySelectorAll('[data-export-run]').forEach(el=>{el.checked=exportSelectedRuns.has(el.dataset.exportRun)});
  document.querySelectorAll('[data-export-sample]').forEach(el=>{
    const ids=exportableRuns().filter(r=>r.sampleId===el.dataset.exportSample).map(r=>r.id),chosen=ids.filter(id=>exportSelectedRuns.has(id)).length;
    el.checked=ids.length>0&&chosen===ids.length;el.indeterminate=chosen>0&&chosen<ids.length;
  });
  document.querySelectorAll('[data-export-date]').forEach(el=>{
    const ids=exportableRuns().filter(r=>{let s=db.samples.find(s=>s.id===r.sampleId);return s&&(s.date||localDateKey(s.created))===el.dataset.exportDate}).map(r=>r.id),chosen=ids.filter(id=>exportSelectedRuns.has(id)).length;
    el.checked=ids.length>0&&chosen===ids.length;el.indeterminate=chosen>0&&chosen<ids.length;
  });
  const count=get('exportCount');if(count)count.textContent=`已选择 ${exportSelectedRuns.size} 项实验`;
}

const renderBeforeMultiExport=render;
render=function(){renderBeforeMultiExport();syncExportSelection()};

exportMarkdown=function(format='md'){
  const selected=exportableRuns().filter(r=>exportSelectedRuns.has(r.id));
  if(!selected.length)return alert('请先勾选至少一项实验');
  const ids=new Set(selected.map(r=>r.sampleId)),oldSamples=db.samples,oldRuns=db.runs;
  let content;
  try{
    db.samples=oldSamples.filter(s=>ids.has(s.id));db.runs=selected;
    content=markdown('all','all','all',!!get('includeExportDetails')?.checked);
  }finally{db.samples=oldSamples;db.runs=oldRuns;}
  const name=`沥青实验记录_自选${selected.length}项_${localDateKey(Date.now())}`;
  if(format==='html')download(name+'.html',horizontalReportHTML(content),'text/html');else download(name+'.md',content,'text/markdown');
};
