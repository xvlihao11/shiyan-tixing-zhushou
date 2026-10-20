const fs=require('fs'),vm=require('vm'),assert=require('assert');
let shown=false;const at=Date.now();
const sample={id:'s',name:'样品',date:'2026-10-20'};
function run(id,minutes){return {id,sampleId:'s',name:id,steps:[{name:'计时',started:at-60000,minutes}]}}
const overdue=run('overdue',.5),timing=run('timing',10),soon=run('soon',2);
const ctx={db:{samples:[sample],runs:[overdue,timing,soon]},current:r=>r.steps.find(s=>s.started&&!s.done),
stepFinished:s=>!!s.done,esc:String,localDateKey:()=>sample.date,get:()=>shown?{}:null};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/dashboard.js','utf8'),ctx);
function selected(key){const html=ctx.dashboardUI();return html.includes(`aria-selected="true" class="active" onclick="chooseTaskTab('${key}')"`)}
assert(selected('timing'));
const html=ctx.dashboardUI();assert(html.indexOf('data-task-id="soon"')<html.indexOf('data-task-id="timing"'));
assert(!html.includes('data-task-id="overdue"'));
shown=true;vm.runInContext("dashboardTab='action'",ctx);assert(selected('action'));
shown=false;assert(selected('timing')); // return from another page
ctx.db.runs=[overdue];assert(selected('action'));
ctx.db.runs=[timing];assert(selected('timing'));
ctx.db.runs=[];assert(ctx.dashboardUI().includes('暂无计时或到点未确认的实验'));
console.log('dashboard timer priority, manual confirmation tab and return navigation passed');
