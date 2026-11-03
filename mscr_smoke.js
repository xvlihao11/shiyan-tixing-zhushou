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
g.readings=['0.12','0.25','63.4'];g.mscrRecoveryStress='3.2';ctx.syncMSCRDiff(g);
assert.strictEqual(g.mscrLegacyThirdValues[0].value,'63.4');assert.strictEqual(g.mscrLegacyThirdValues[0].stress,'3.2');assert.strictEqual(g.readings[2],'108.3');assert(ctx.resultsComplete(r));
ctx.syncMSCRDiff(g);assert.strictEqual(g.mscrLegacyThirdValues.length,1);
assert.strictEqual(ctx.avg(r,g),'Jnr₀.₁ = 0.12 kPa⁻¹；Jnr₃.₂ = 0.25 kPa⁻¹；Jnr diff = 108.3 %');
const ui=ctx.groupUI(r,g,0);assert.strictEqual((ui.match(/id="reading_/g)||[]).length,3);for(const label of ['Jnr₀.₁','Jnr₃.₂','Jnr diff'])assert(ui.includes(label));assert(ui.includes('readonly'));assert(!ui.includes('MSCR 恢复率应力'));assert(!ui.includes('remove-reading'));assert(!ui.includes('＋增加读数'));
let report=ctx.markdown('s');assert.strictEqual(report.split('\n').filter(l=>l.startsWith('| 试样 |')).length,3);assert(report.includes('| kPa⁻¹ | 0.12 |'));assert(report.includes('| kPa⁻¹ | 0.25 |'));assert(report.includes('| % | 108.3 |'));assert(report.includes('(0.25 − 0.12) ÷ 0.12 × 100%'));assert(!report.includes('蠕变恢复率'));assert(!report.includes('63.4'));
for(const [low,high,expected] of [['1','1','0.0'],['1','3','200.0'],['1','0.5','-50.0'],['1','0','-100.0'],['0.1','0.151','51.0']]){g.readings=[low,high,''];assert.strictEqual(ctx.mscrDiff(g),expected);assert(ctx.resultsComplete(r));}
for(const readings of [['0.1','',''],['-0.1','0.2',''],['0','0.2',''],['0','0',''],['0.1','-1',''],['0.1','NaN','']]){g.readings=readings;assert(!ctx.resultsComplete(r));assert.strictEqual(ctx.avg(r,g),'—')}
// Editing the first two fields updates the stored computed field without mixing quantities.
for(const [i,value] of ['1.02','1.2',''].entries())fields.set('reading_'+g.id+'_'+i,{value});
ctx.collectResults(r);assert.strictEqual(g.readings[2],'17.6');assert.strictEqual(g.readings[0],'1.02');
g.readings=['0.89','1.2',''];ctx.syncMSCRDiff(g);assert.strictEqual(g.readings[2],'34.8');
template.name='我的蠕变试验';assert(ctx.isMSCR(template));ctx.ensureMSCRTemplate();assert.strictEqual(ctx.db.templates.length,1);
assert(!html.includes('MSCR试验设置</button>'));assert(html.includes('ensureBBRTemplate();ensureMSCRTemplate();return'));
console.log('MSCR Jnr diff calculation, live updates, zero denominator, old R retention and separate export passed');
