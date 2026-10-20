let dashboardTab=null,dashboardExpanded=false,dashboardSignature='';
function taskBuckets(at=Date.now()){
  const buckets={timing:[],action:[]};
  for(const run of db.runs){
    const sample=db.samples.find(s=>s.id===run.sampleId),step=current(run);
    if(!sample||run.completedAt||!step||stepFinished(step)||!(step.minutes>0))continue;
    const item={run,sample,step,index:run.steps.indexOf(step),due:step.started+step.minutes*60000,label:step.name};
    buckets[item.due>at?'timing':'action'].push(item);
  }
  for(const items of Object.values(buckets))items.sort((a,b)=>a.due-b.due||a.run.id.localeCompare(b.run.id));
  return buckets;
}
function taskDuration(ms){const seconds=Math.max(0,Math.ceil(ms/1000)),h=Math.floor(seconds/3600),m=Math.floor(seconds%3600/60),s=seconds%60;return h?`${h}时${m}分${s}秒`:`${m}分${s}秒`}
function taskStamp(item,tab){return tab==='timing'?'剩余 '+taskDuration(item.due-Date.now()):'已到点 · 超时 '+taskDuration(Date.now()-item.due)}
function dashboardUI(){
  const buckets=taskBuckets(),total=Object.values(buckets).reduce((n,a)=>n+a.length,0);
  dashboardSignature=JSON.stringify(Object.entries(buckets).map(([k,a])=>[k,a.map(x=>x.run.id)]));
  if(!total)return '<section id="taskDashboard" class="task-dashboard task-empty"><strong>当前任务</strong><span>暂无计时或到点未确认的实验</span></section>';
  // Each visit starts with active timers; manual tab selection stays available.
  if(!get('taskDashboard')||!buckets[dashboardTab]||!buckets[dashboardTab].length)dashboardTab=buckets.timing.length?'timing':'action';
  const rows=buckets[dashboardTab],visible=dashboardExpanded?rows:rows.slice(0,3),names={timing:'计时中',action:'到点待确认'};
  return `<section id="taskDashboard" class="task-dashboard"><div class="task-heading"><h2>当前任务</h2><small>跨日期汇总</small></div><div class="task-tabs" role="tablist" aria-label="当前任务分类">${Object.entries(names).map(([key,name])=>`<button role="tab" aria-selected="${dashboardTab===key}" class="${dashboardTab===key?'active':''}" onclick="chooseTaskTab('${key}')">${name}<b>${buckets[key].length}</b></button>`).join('')}</div><div class="task-list">${visible.map(x=>`<article class="task-row ${dashboardTab==='timing'?'task-row-timing':''}" data-task-id="${x.run.id}" onclick="openTask('${x.run.id}')"><div class="task-row-head"><strong>${esc(x.sample.name)}</strong><span class="task-clock ${dashboardTab}" data-run-clock="${x.run.id}">${esc(taskStamp(x,dashboardTab))}</span></div><p>${esc((x.run.phase&&x.run.phase!=='原样'?x.run.phase+' · ':'')+x.run.name)} · ${esc(x.label)}</p><div class="task-row-foot"><small>${esc(x.sample.date||localDateKey(x.sample.created))}</small><button class="${dashboardTab==='action'?'':'secondary'}" onclick="event.stopPropagation();taskAction('${x.run.id}','${dashboardTab}')">${dashboardTab==='timing'?'查看实验':'确认完成'}</button></div></article>`).join('')||'<p class="task-none">这类任务已全部处理</p>'}</div>${rows.length>3?`<button class="task-show-more" onclick="toggleAllTasks()">${dashboardExpanded?'收起':'查看全部 '+rows.length+' 项'} ⌄</button>`:''}</section>`;
}
function redrawDashboard(){const el=get('taskDashboard');if(el)el.outerHTML=dashboardUI()}
function chooseTaskTab(tab){dashboardTab=tab;dashboardExpanded=false;redrawDashboard()}
function toggleAllTasks(){dashboardExpanded=!dashboardExpanded;redrawDashboard()}
function openTask(id){const run=db.runs.find(r=>r.id===id),sample=run&&db.samples.find(s=>s.id===run.sampleId);if(sample)routeTo('samples',sample.date||localDateKey(sample.created),sample.id,id)}
function taskAction(id,tab){if(tab!=='action'){openTask(id);return}const item=taskBuckets().action.find(x=>x.run.id===id);if(!item){redrawDashboard();return}finishStep(id,item.index)}
function refreshDashboard(){
  if(!get('taskDashboard'))return;const buckets=taskBuckets();
  const signature=JSON.stringify(Object.entries(buckets).map(([k,a])=>[k,a.map(x=>x.run.id)]));
  if(signature!==dashboardSignature){redrawDashboard();return}
  for(const [tab,items] of Object.entries(buckets))for(const item of items){const el=document.querySelector(`[data-run-clock="${item.run.id}"]`);if(el)el.textContent=taskStamp(item,tab)}
}
