const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(a,b){const i=html.indexOf(a),j=html.indexOf(b,i);assert(i>=0&&j>i,a);return html.slice(i,j)}
const fields=new Map(),ctx={db:{templates:[],samples:[{id:'s',name:'试样',sampleCount:1,date:'2026-11-03'}],runs:[]},KEY:'test',window:{},localStorage:{setItem(){}},get:id=>fields.get(id)||null,esc:v=>String(v??''),uid:()=>Math.random().toString(36).slice(2),alert(){},confirm:()=>true,render(){},isAgingMassLoss:()=>false,isHeating:()=>false,isSoftening:()=>false,isSegregation:()=>false,resultUnit:()=>'',sampleChoices:()=>['试样'],photoGroupUI:()=>'',evaluateInternal:()=>({state:'neutral'}),exportSamplesFor:()=>ctx.db.samples,localDateKey:()=> '2026-11-03'};
vm.createContext(ctx);for(const asset of ['mscr.js','bbr.js'])vm.runInContext(fs.readFileSync(__dirname+'/app/assets/'+asset,'utf8'),ctx);
vm.runInContext(part('function templateValueOptions(', 'function closeRunOptions('),ctx);
vm.runInContext(part(' function addRun(', 'function toggleRunMenu('),ctx);
vm.runInContext(part('function groupUI(', 'function setGroupNameChoice('),ctx);
vm.runInContext(part('function resultsComplete(', 'function normalizeRotationRun('),ctx);
vm.runInContext(part('function resultQuantum(', 'function derivedMarkdown('),ctx);
vm.runInContext(part('const md=x=>', 'function download('),ctx);
vm.runInContext(part('function collectResults(', ' function saveResultsDraft('),ctx);
ctx.ensureMSCRTemplate();ctx.ensureMSCRTemplate();assert.strictEqual(ctx.db.templates.length,1);
const template=ctx.db.templates[0];assert(ctx.isMSCR(template));assert(template.standard.includes('T 0647'));
ctx.addRun('s',template.id,false);const r=ctx.db.runs[0],g=r.groups[0];assert.strictEqual(g.readings.length,3);assert.strictEqual(r.resultKind,'mscr');
g.readings=['0.12','0.25','63.4'];assert(!ctx.resultsComplete(r));g.mscrRecoveryStress='3.2';assert(ctx.resultsComplete(r));
assert.strictEqual(ctx.avg(r,g),'Jnr₀.₁ = 0.12 kPa⁻¹；Jnr₃.₂ = 0.25 kPa⁻¹；R(3.2 kPa) = 63.4 %');
const ui=ctx.groupUI(r,g,0);assert.strictEqual((ui.match(/id="reading_/g)||[]).length,3);for(const label of ['Jnr₀.₁','Jnr₃.₂','蠕变恢复率'])assert(ui.includes(label));assert(!ui.includes('remove-reading'));assert(!ui.includes('＋增加读数'));
let report=ctx.markdown('s');assert.strictEqual(report.split('\n').filter(l=>l.startsWith('| 试样 |')).length,3);assert(report.includes('| kPa⁻¹ | 0.12 |'));assert(report.includes('| kPa⁻¹ | 0.25 |'));assert(report.includes('| % | 63.4 |'));assert(report.includes('应力 3.2 kPa'));
g.mscrRecoveryStress='0.1';report=ctx.markdown('s');assert(report.includes('R · 0.1 kPa 蠕变恢复率'));assert(!report.includes('R · 3.2 kPa 蠕变恢复率'));
for(const readings of [['0','0','0'],['0.012345','0.999999','100']]){g.readings=readings.slice();assert(ctx.resultsComplete(r));ctx.markdown('s');assert.deepStrictEqual(g.readings,readings)}
for(const readings of [['0.1','','60'],['-0.1','0.2','60'],['0.1','0.2','101'],['0.1','0.2','-1'],['0.1','0.2','NaN']]){g.readings=readings;assert(!ctx.resultsComplete(r));assert.strictEqual(ctx.avg(r,g),'—')}
template.name='我的蠕变试验';assert(ctx.isMSCR(template));ctx.ensureMSCRTemplate();assert.strictEqual(ctx.db.templates.length,1);
assert(!html.includes('MSCR试验设置</button>'));assert(html.includes('ensureBBRTemplate();ensureMSCRTemplate();return'));
console.log('MSCR distinct Jnr0.1/Jnr3.2/R fields, stress binding, completion, raw precision and export passed');
