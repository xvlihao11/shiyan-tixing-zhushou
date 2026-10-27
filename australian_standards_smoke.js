const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(start,end){const a=html.indexOf(start),b=html.indexOf(end,a);assert(a>=0&&b>a);return html.slice(a,b)}
for(const script of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g))new vm.Script(script[1]);
const original={id:'old',sampleId:'s',templateId:'soft',name:'软化点',standard:'JTG 3410—2025 / T 0606—2011',unit:'℃',groups:[{name:'试样',readings:['63.49','63.96']}]};
const ctx={KEY:'test',db:{templates:[{id:'soft',name:'软化点',standard:'原有国内规范',temps:'5,32',steps:[{minutes:5}]},{id:'pen',name:'针入度',standard:'GB/T 4509—2010'},{id:'other',name:'延度',standard:'自定义规程'},{id:'ratio',name:'针入度比',standard:'不变'}],runs:[original],samples:[{id:'s',name:'试样',date:'2026-10-20'}]},
localStorage:{setItem(){}},isAgingMassLoss:()=>false,isSoftening:r=>r.name.includes('软化点'),isHeating:()=>false,
isSegregation:()=>false,rotationValueInPa:(r,v)=>Number(v),resultUnit:r=>r.unit,evaluateInternal:()=>({state:'neutral'}),
exportSamplesFor:()=>ctx.db.samples,localDateKey:()=> '2026-10-20'};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/bbr.js','utf8'),ctx);
vm.runInContext(part('function templateValueOptions(', 'function normalizeRunTemperature('),ctx);
const prior=JSON.stringify(original);
vm.runInContext(part('function metricStandardOptions(', 'const esc='),ctx);
assert(ctx.db.templates[0].standard.includes('AS 2341.18:2020'));
assert(ctx.db.templates[0].standard.startsWith('原有国内规范'));
assert(ctx.db.templates[1].standard.includes('AS 2341.12:2020'));
assert.strictEqual(ctx.db.templates[0].temps,'5,32');assert.strictEqual(ctx.db.templates[0].steps[0].minutes,5);
assert.strictEqual(ctx.db.templates[2].standard,'自定义规程');assert.strictEqual(ctx.db.templates[3].standard,'不变');
assert.strictEqual(JSON.stringify(original),prior);
const first=JSON.stringify(ctx.db.templates);ctx.ensureMetricStandards();assert.strictEqual(JSON.stringify(ctx.db.templates),first);
vm.runInContext(part('function resultQuantum(', 'function derivedMarkdown('),ctx);
vm.runInContext(part('const md=x=>','function download('),ctx);
assert.strictEqual(ctx.avg(original,original.groups[0]),'64.0');
original.standard='AS 2341.18:2020';
assert.strictEqual(ctx.avg(original,original.groups[0]),'—');
original.softeningBath='water';assert.strictEqual(ctx.avg(original,original.groups[0]),'63.8');
let report=ctx.markdown('s');assert(report.includes('| ℃ | 63.8 | 63.49、63.96 | 水浴；'));assert(report.includes('AS 2341.18:2020'));
original.softeningBath='glycerol';assert.strictEqual(ctx.avg(original,original.groups[0]),'63.5');
original.standard='JTG 3410—2025 / T 0606—2011';assert.strictEqual(ctx.avg(original,original.groups[0]),'64.0');
assert.strictEqual(JSON.stringify(original.groups[0].readings),'["63.49","63.96"]');
const pen={name:'针入度',standard:'AS 2341.12:2020',groups:[{readings:['19','20','21']}]};assert.strictEqual(ctx.avg(pen,pen.groups[0]),'20');
const flashTemplate={id:'flash',name:'闪点',standard:'T 0611',temps:'',steps:[{minutes:0}]};
ctx.db.templates.push(flashTemplate);ctx.ensureMetricStandards();
assert.strictEqual(flashTemplate.standard,'T 0611, AS 2341.14:2013');
assert.strictEqual(ctx.metricStandardOptions({name:'闭口闪点'}).length,0);
const flash={id:'old-flash',sampleId:'s',name:'闪点',standard:'T 0611',unit:'℃',groups:[{name:'试样',readings:['278']}]};
ctx.db.runs=[flash];assert.strictEqual(ctx.avg(flash,flash.groups[0]),'278');
assert(ctx.runStandardOptions(flash).includes('AS 2341.14:2013'));
flash.standard='AS 2341.14:2013';assert.strictEqual(ctx.avg(flash,flash.groups[0]),'280');
assert(ctx.markdown('s').includes('| ℃ | 280 | 278 |'));assert(ctx.markdown('s').includes('AS 2341.14:2013'));
assert.strictEqual(flash.groups[0].readings[0],'278');
flash.standard='T 0611';assert.strictEqual(ctx.avg(flash,flash.groups[0]),'278');
console.log('Australian option migration, method/bath switching, old records and report passed');
