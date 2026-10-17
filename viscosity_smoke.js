const fs=require('fs');
const vm=require('vm');
const assert=require('assert');
const path=require('path');
const html=fs.readFileSync(path.join(__dirname,'app/assets/index.html'),'utf8');
const internal=fs.readFileSync(path.join(__dirname,'app/assets/internal_standards.js'),'utf8');
function part(text,start,end){const i=text.indexOf(start),j=text.indexOf(end,i);assert(i>=0&&j>i,start);return text.slice(i,j)}
const source=[
  part(html,'function resultQuantum(', 'function derivedMarkdown('),
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
vm.createContext(ctx);vm.runInContext(source,ctx);
const original={id:'before',sampleId:'s',templateId:'t1',phase:'原样',name:'60 ℃动力黏度',temp:'60',unit:'Pa·s',groups:[{name:'基质沥青',readings:['100']}]};
const aged={id:'after',sampleId:'s',templateId:'t2',phase:'RTFOT 老化后',name:'60 ℃动力粘度',temp:'60',unit:'Pa·s',groups:[{name:'基质沥青',readings:['270']}]};
ctx.db.runs=[original,aged];
let rows=ctx.derivedResults('s');
assert.strictEqual(rows.length,1);
assert.strictEqual(rows[0].value,'2.7 倍（270%）');
assert.strictEqual(rows[0].evaluation.state,'pass');
assert.strictEqual(rows[0].evaluation.value,270);
assert(ctx.derivedUI('s','after').includes('合格'));
const report=ctx.markdown('s');
assert(report.includes('60 ℃动力粘度 · 黏度比'));
assert(report.includes('✅ 合格（内控 ≤300 %）'));
assert(!report.includes('### 老化前后计算结果'));
assert(!ctx.markdown('s','before').includes('黏度比'));
aged.groups[0].readings=['301'];
rows=ctx.derivedResults('s');
assert.strictEqual(rows[0].evaluation.state,'fail');
assert.strictEqual(rows[0].evaluation.value,301);
ctx.db.samples[0].internalProfileId='vg40';
original.unit=aged.unit='Poises';
original.groups[0].readings=['4000'];
aged.groups[0].readings=['14000'];
assert.strictEqual(ctx.derivedResults('s')[0].evaluation.state,'pass');
aged.groups[0].readings=['14040'];
assert.strictEqual(ctx.derivedResults('s')[0].evaluation.state,'fail');
aged.temp='80';
assert.strictEqual(ctx.derivedResults('s').length,0);
aged.temp='60';
original.groups[0].readings=[''];
assert.strictEqual(ctx.derivedResults('s').length,0);
console.log('dynamic viscosity ratio and internal limit smoke passed');
