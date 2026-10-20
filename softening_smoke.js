const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(start,end){const a=html.indexOf(start),b=html.indexOf(end,a);assert(a>=0&&b>a);return html.slice(a,b)}
const ctx={db:{samples:[{id:'s',name:'EME2',date:'2026-10-20'}],runs:[]},
  isAgingMassLoss:()=>false,isSoftening:r=>r.name.includes('软化点'),
  isSegregation:(r,g)=>r.name.includes('软化点')&&g.calculationMode==='segregation',
  rotationValueInPa:(r,v)=>Number(v),isHeating:()=>false,resultUnit:r=>r.unit,
  exportSamplesFor:()=>ctx.db.samples,localDateKey:()=> '2026-10-20',evaluateInternal:()=>({state:'neutral'})};
vm.createContext(ctx);
vm.runInContext(part('function resultQuantum(', 'function derivedMarkdown('),ctx);
vm.runInContext(part('const md=x=>','function download('),ctx);
const group={name:'EME2',readings:['63.49','63.96']};
const run={id:'old',sampleId:'s',name:'软化点',unit:'℃',phase:'原样',groups:[group]};ctx.db.runs=[run];
assert.strictEqual(ctx.avg(run,group),'64.0');
const report=ctx.markdown('s');assert(report.includes('| ℃ | 64.0 | 63.49、63.96 |'));
assert.strictEqual(JSON.stringify(group.readings),'["63.49","63.96"]');
for(const [values,result] of [[['63.25','63.25'],'63.0'],[['63.75','63.75'],'64.0'],[['63.0','63.5'],'63.0'],[['63.5','64.0'],'64.0'],[['64.0','64.5'],'64.0'],[['64.5','65.0'],'65.0'],[['',''],'—']]){
  group.readings=values;assert.strictEqual(ctx.avg(run,group),result,JSON.stringify(values));
}
// Segregation remains the absolute difference of the original upper/lower values.
group.calculationMode='segregation';group.readings=['63.49','63.96'];assert.strictEqual(ctx.avg(run,group),'0.47');
// Non-softening metrics retain their existing average calculation.
group.calculationMode='average';run.name='针入度';group.readings=['63.49','63.96'];assert.strictEqual(ctx.avg(run,group),'64');
console.log('softening measured-value and mean rounding, ties-to-even and existing export passed');
