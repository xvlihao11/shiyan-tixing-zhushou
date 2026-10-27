const fs=require('fs');
const vm=require('vm');
const assert=require('assert');
const path=require('path');
const html=fs.readFileSync(path.join(__dirname,'app/assets/index.html'),'utf8');
const internal=fs.readFileSync(path.join(__dirname,'app/assets/internal_standards.js'),'utf8');
function part(text,start,end){const i=text.indexOf(start),j=text.indexOf(end,i);assert(i>=0&&j>i,start);return text.slice(i,j)}
const source=[
  "function isRotationalViscosity(r){return /旋转[黏粘]度/.test(String(r?.name||''))}",
  "function normalizedViscosityUnit(unit){return String(unit||'').replace(/\\s/g,'').replace(/[·。]/g,'.').toLowerCase()}",
  "function rotationInputIsMilli(r){return isRotationalViscosity(r)&&/^mpa(?:\\.?)s?$/.test(normalizedViscosityUnit(r.rotationInputUnit||r.unit))}",
  "function rotationValueInPa(r,value){const n=Number(value);return Number.isFinite(n)?(rotationInputIsMilli(r)?n/1000:n):n}",
  part(html,'function resultQuantum(', 'function derivedMarkdown('),
  part(html,'function exportResult(', 'function markdown('),
  part(internal,'function parseInternalLimit(', 'function resolveInternalBinding('),
  part(internal,'function evaluateDerivedViscosity(', 'function internalMetricCandidates('),
  part(html,'const md=x=>', 'function download('),
].join('\n');
const c170={id:'c170',rows:[{item:'RTFOT 后残留粘度比（60℃）',limit:'≤300',unit:'%'}]};
const vg40={id:'vg40',rows:[{item:'RTFOT 后粘度比（60℃）',limit:'≤3.5',unit:'—'}]};
const ctx={db:{samples:[{id:'s',date:'2026-10-15',name:'试样',sampleCount:1,internalProfileId:'c170'}],runs:[]},
  internalProfile:s=>s?.internalProfileId==='c170'?c170:s?.internalProfileId==='vg40'?vg40:null,
  isAgingMassLoss:()=>false,isSegregation:()=>false,isHeating:()=>false,esc:x=>String(x),
  isSoftening:()=>false,resultUnit:r=>r.unit,evaluateInternal:()=>({state:'neutral'}),
  exportSamplesFor:()=>ctx.db.samples,localDateKey:()=> '2026-10-15',
  internalVerdictHTML:r=>r.text};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/bbr.js','utf8'),ctx);vm.runInContext(source,ctx);

vm.runInContext(html.match(/function isAgingMassLoss\(r\)\{[^\n]+/)[0],ctx);
vm.runInContext(part(html,'const AGING_MASS_LABELS=', 'function groupUI('),ctx);
ctx.rotationUnitLabel=r=>r.unit;
const mk=(id,name,phase,readings,unit='Pa·s',temp='60')=>({id,sampleId:'s',templateId:name,phase,name,unit,temp,groups:[{name:'试样',readings}],steps:[]});
ctx.db.runs=[mk('mass','RTFOT 老化','老化处理',['137.5990','172.5527','172.5769'],'g'),mk('v0','动力黏度','原样',['100']),mk('p0','针入度','原样',['50'],'0.1mm','25'),mk('s0','软化点','原样',['50','50'],'℃','5'),mk('v1','动力黏度','RTFOT 老化后',['270']),mk('p1','针入度','RTFOT 老化后',['40'],'0.1mm','25'),mk('s1','软化点','RTFOT 老化后',['55','55'],'℃','5')];
const snapshot=JSON.stringify(ctx.db);
const summary=ctx.derivedUI('s');
for(const label of ['黏度比','针入度比','软化点差','质量变化（质量损失）','+0.069'])assert(summary.includes(label),label);
assert.strictEqual((summary.match(/card derived-results/g)||[]).length,1);
const report=ctx.markdown('s');
assert(!report.includes('### 老化前后计算结果'));
const table=report.split('\n').filter(l=>l.startsWith('| 试样 |'));
assert.strictEqual(table.length,10);
assert(table.slice(-4).every((line,i)=>line.includes(['黏度比','针入度比','软化点差','RTFOT 老化'][i])));
assert.strictEqual((report.match(/\| % \| \+0\.069 \|/g)||[]).length,1);
assert(!ctx.markdown('s','p0').includes('针入度比'));
assert(!ctx.derivedUI('s','v1').includes('质量变化（质量损失）'));
assert.strictEqual(JSON.stringify(ctx.db),snapshot);
ctx.db.runs[0].groups[0].readings[2]='';
assert(!ctx.derivedUI('s').includes('质量变化（质量损失）'));
console.log('aging summary groups all four metrics; export has adjacent, nonduplicate rows; data preserved');
