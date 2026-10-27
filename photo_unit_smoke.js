const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(start,end){return html.slice(html.indexOf(start),html.indexOf(end,html.indexOf(start)))}
let alerts=[];
const ctx={db:{runs:[]},window:{},KEY:'test',localStorage:{setItem(){}},get:()=>null,page:'export',render(){},
  alert:m=>alerts.push(m),confirm:()=>true,isAgingMassLoss:()=>false,isSoftening:()=>false,
  resultsComplete:()=>false,completeRun(){},console};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/bbr.js','utf8'),ctx);
vm.runInContext(part('function isRotationalViscosity(', 'const AGING_MASS_LABELS='),ctx);
vm.runInContext(part('function normalizeRotationRun(', ' function completeRun('),ctx);
vm.runInContext(fs.readFileSync(__dirname+'/app/assets/cloud.js','utf8'),ctx);
for(const unit of ['mPa.s','mpa.s','mPa·s','mPas','cP'])assert.strictEqual(ctx.photoViscosityUnit(unit),'mPa·s');
assert.strictEqual(ctx.photoViscosityUnit('Pa.s'),'Pa·s');
function photoRun(unit,existing='',inputUnit='Pa·s'){
  const a={id:'a',status:'loading'},p={id:'p',attempts:[a]},g={id:'g',readings:[existing,'',''],photos:[p]};
  const r={id:'r',name:'旋转粘度',unit:'Pa·s',rotationInputUnit:inputUnit,groups:[g]};
  ctx.db.runs=[r];
  ctx.window.onNativePhotoEvent({type:'result',runId:'r',groupId:'g',payload:{photoId:'p',attemptId:'a',readings:[{label:'旋转粘度1',value:'1250',unit}]}});
  return {a,g,r};
}
let {a,g,r}=photoRun('mPa.s');
assert.strictEqual(a.candidates[0].unit,'mPa.s');
assert.strictEqual(a.reviewUnits[0],'mPa·s');
ctx.applyPhotoReadings('g','p','a');
assert.strictEqual(g.readings[0],'1.25');assert.strictEqual(r.rotationInputUnit,'Pa·s');
// Repeat recognition in a previously completed Pa·s record: convert exactly once.
({a,g,r}=photoRun('mPa.s','1.25'));ctx.applyPhotoReadings('g','p','a');assert.strictEqual(g.readings[0],'1.25');
({a,g,r}=photoRun('Pa.s'));ctx.applyPhotoReadings('g','p','a');assert.strictEqual(g.readings[0],'1250');
({a,g,r}=photoRun(''));ctx.applyPhotoReadings('g','p','a');assert.strictEqual(g.readings[0],'');assert(alerts.length);
ctx.editPhotoUnit('g','p','a',0,'mPa·s');ctx.applyPhotoReadings('g','p','a');assert.strictEqual(g.readings[0],'1.25');
// Other groups' pre-existing milli readings must also be normalized once.
({a,g,r}=photoRun('mPa.s','500','mPa·s'));r.groups.push({id:'other',readings:['2000']});
ctx.applyPhotoReadings('g','p','a');assert.strictEqual(g.readings[0],'1.25');assert.strictEqual(r.groups[1].readings[0],'2');
console.log('photo unit retention, review and one-time conversion passed');
