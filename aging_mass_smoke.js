const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(start,end){const a=html.indexOf(start),b=html.indexOf(end,a);assert(a>=0&&b>a);return html.slice(a,b)}
const ctx={db:{samples:[{id:'s',name:'EME2',date:'2026-10-20'}],runs:[]},
  exportSamplesFor:()=>ctx.db.samples,isHeating:()=>false,isSegregation:()=>false,isSoftening:()=>false,
  localDateKey:()=> '2026-10-20',evaluateInternal:()=>({state:'neutral'}),rotationUnitLabel:r=>r.unit};
vm.createContext(ctx);
vm.runInContext(html.match(/function isAgingMassLoss\(r\)\{[^\n]+/)[0],ctx);
vm.runInContext(part('const AGING_MASS_LABELS=', 'function groupUI('),ctx);
vm.runInContext(part('function resultQuantum(', 'function derivedMarkdown('),ctx);
vm.runInContext(part('const md=x=>','function download('),ctx);
const group={name:'EME2',readings:['137.5990','172.5527','172.5769']};
const run={id:'r',sampleId:'s',name:'RTFOT 老化',phase:'老化处理',unit:'g',groups:[group]};ctx.db.runs=[run];
const before=JSON.stringify(group.readings);
assert(Math.abs(ctx.agingMassStatus(group).value-0.069234443849)<0.000001);
assert.strictEqual(ctx.avg(run,group),'+0.069');
assert(ctx.markdown('s').includes('| % | +0.069 |'));
assert.strictEqual(JSON.stringify(group.readings),before);
group.massCorrection='0.006';assert.strictEqual(ctx.avg(run,group),'+0.075');
assert(ctx.markdown('s').includes('ΔF：0.006 %'));
group.massCorrection='0';group.readings[2]='172.5285';assert.strictEqual(ctx.avg(run,group),'-0.069');
run.name='TFOT 老化';assert.strictEqual(ctx.avg(run,group),'-0.069');
group.readings[2]=group.readings[1];assert.strictEqual(ctx.avg(run,group),'0.000');
group.readings[2]='172.552699';assert.strictEqual(ctx.avg(run,group),'0.000');
group.readings[0]=group.readings[1];assert.strictEqual(ctx.agingMassStatus(group).value,null);
group.readings=['1','2',''];assert.strictEqual(ctx.avg(run,group),'—');
group.readings=['1','2','3'];group.massCorrection='bad';assert.strictEqual(ctx.avg(run,group),'—');
console.log('aging mass gain/loss signs, correction and existing-record export passed');
