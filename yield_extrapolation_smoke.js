const fs=require('fs'),vm=require('vm'),assert=require('assert');
const ctx={yieldFmt:n=>Number(n).toFixed(2),console};vm.createContext(ctx);
for(const file of ['yield_extrapolation.js','yield_local.js'])vm.runInContext(fs.readFileSync(__dirname+'/app/assets/'+file,'utf8'),ctx);
const two=[{y:40.83,p:42},{y:42.83,p:70}];
let result=ctx.yieldCurveEstimate(two,'p',68,'y');assert(!result.extrapolated);assert(Math.abs(result.value-42.68714285714285)<1e-9);
result=ctx.yieldCurveEstimate(two,'p',80,'y');assert(result.extrapolated);assert.strictEqual(result.edgeDistance,10);assert(Math.abs(result.value-43.544285714285714)<1e-9);
let inverse=ctx.yieldCurveEstimate(two,'y',result.value,'p');assert(inverse.extrapolated);assert(Math.abs(inverse.value-80)<1e-9);
const three=[{y:55,p:20},{y:57,p:40},{y:61,p:100}];
assert.strictEqual(ctx.yieldCurveEstimate(three,'p',130,'y').value,63);assert.strictEqual(ctx.yieldCurveEstimate(three,'p',10,'y').value,54);assert.strictEqual(ctx.yieldCurveEstimate(three,'y',63,'p').value,130);
assert(ctx.yieldCurveEstimate(three,'y',50,'p').error);assert(ctx.yieldCurveEstimate(three,'y',101,'p').error);assert(ctx.yieldCurveEstimate(three,'p',0,'y').error);
assert(ctx.yieldCurveEstimate([{y:98,p:30},{y:99,p:40}],'p',60,'y').error);assert(ctx.yieldCurveEstimate([{y:55,p:40},{y:57,p:30}],'p',50,'y').error);
assert(ctx.yieldEstimateNote(result,'p').includes('外推估算'));assert(ctx.yieldEstimateNote(result,'p').includes('不是实测'));
// Opening/closing the HTML oil picker preserves form state and restores scrolling.
const nodes=new Map(),draft={oilA:'a'},library=[{id:'a',name:'母油',batch:'A'},{id:'b',name:'调配油',batch:'B'}];
const ui={db:{yieldOilLibrary:library,yieldOilDraft:draft},KEY:'test',localStorage:{setItem(){}},get:id=>nodes.get(id),esc:v=>String(v),render(){},yieldValid:(n,a,b)=>n>=a&&n<=b,yieldFmt:String,
  document:{
    body:{style:{overflow:''},append(el){nodes.set(el.id,el)}},
    createElement(){return {dataset:{},setAttribute(){},querySelector:()=>({focus(){}}),remove(){nodes.delete(this.id)}}}
  }
};
vm.createContext(ui);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/oil_evaluation.js','utf8'),ui);
ui.openOilPicker('oilA');assert(nodes.get('oilPickerDialog').innerHTML.includes('母油'));assert.strictEqual(ui.document.body.style.overflow,'hidden');ui.chooseOilPicker('oilA','b');assert.strictEqual(draft.oilA,'b');assert.strictEqual(ui.document.body.style.overflow,'');assert(!nodes.has('oilPickerDialog'));
console.log('Edge extrapolation, in-range equivalence, physical limits and HTML oil-picker selection passed');
