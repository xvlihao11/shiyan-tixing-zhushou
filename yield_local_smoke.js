const fs=require('fs'),vm=require('vm'),assert=require('assert');
const fields=new Map(),set=(id,value)=>fields.set(id,{value:String(value)});
let resultText='';const result={get innerHTML(){return resultText},set innerHTML(text){resultText=text;const m=text.match(/id="chosenYield"[^>]*value="([^"]*)"/);if(m)set('chosenYield',m[1])}};
fields.set('yieldResult',result);fields.set('yieldChoice',{innerHTML:'',textContent:''});
const observations=[{id:'a',type:'cut',grade:'G',batch:'NEW',yieldPct:40.83,pen:42},{id:'b',type:'cut',grade:'G',batch:'NEW',yieldPct:42.83,pen:70},{id:'old',type:'cut',grade:'G',batch:'OLD',yieldPct:62,pen:42},{id:'old2',type:'cut',grade:'G',batch:'OLD',yieldPct:66,pen:70},{id:'other',type:'cut',grade:'OTHER',batch:'NEW',yieldPct:90,pen:80},{id:'wax',type:'wax',grade:'G',batch:'NEW',initialYield:40,yieldChangePct:10,basePen:42,finalPen:70}];
const ctx={db:{yieldObservations:observations,yieldGrades:['G'],yieldDrafts:{}},KEY:'test',localStorage:{setItem(){}},get:id=>fields.get(id)||null,esc:x=>String(x??''),console};vm.createContext(ctx);
for(const file of ['yield_local.js','yield.js'])vm.runInContext(fs.readFileSync(__dirname+'/app/assets/'+file,'utf8'),ctx);
vm.runInContext("yieldGrade='G';yieldSelected=new Set(db.yieldObservations.map(o=>o.id));",ctx);
for(const [id,value] of Object.entries({calcMode:'calibrated',calcBatch:'NEW',calcPen:42,calcYield:40.83,calcTarget:68,calcMass:100}))set(id,value);
const before=JSON.stringify(ctx.db.yieldObservations);
let curve=ctx.yieldLocalCurve('G','NEW',42,40.83);assert.strictEqual(curve.points.length,2);assert.strictEqual(curve.ignored,4);
const predicted=ctx.yieldLocalInterpolate(curve.points,'p',68,'y');assert(Math.abs(predicted-42.68714285714285)<1e-9);
assert(Math.abs(ctx.yieldLocalInterpolate(curve.points,'y',predicted,'p')-68)<1e-9);
assert.strictEqual(ctx.yieldLocalInterpolate(curve.points,'p',42,'y'),40.83);assert.strictEqual(ctx.yieldLocalInterpolate(curve.points,'p',70,'y'),42.83);
ctx.calculateYield();assert(resultText.includes('42.69%'));assert(!resultText.includes('预计加蜡油'));assert(!resultText.includes('43.5%'));assert(fields.get('yieldChoice').innerHTML.includes('68.04'));
set('calcTarget',75);ctx.calculateYield();assert(resultText.includes('超出本批已测'));assert(!resultText.includes('建议收率'));
set('calcBatch','UNKNOWN');ctx.calculateYield();assert(resultText.includes('至少需要两个'));assert(!resultText.includes('按原经验'));
// Completely different feedstocks and nonlinear local slopes work without code changes.
ctx.db.yieldObservations=[{id:'c',type:'cut',grade:'G',batch:'THIRD',yieldPct:55,pen:20},{id:'d',type:'cut',grade:'G',batch:'THIRD',yieldPct:57,pen:40},{id:'e',type:'cut',grade:'G',batch:'THIRD',yieldPct:61,pen:100}];
vm.runInContext('yieldSelected=new Set(db.yieldObservations.map(o=>o.id))',ctx);
curve=ctx.yieldLocalCurve('G','THIRD',20,55);assert.strictEqual(curve.points.length,3);assert.strictEqual(ctx.yieldLocalInterpolate(curve.points,'p',70,'y'),59);assert.strictEqual(ctx.yieldLocalInterpolate(curve.points,'p',30,'y'),56);
assert.strictEqual(ctx.yieldLocalInterpolate(curve.points,'p',120,'y'),null);
ctx.db.yieldObservations.push({id:'duplicate',type:'cut',grade:'G',batch:'THIRD',yieldPct:57,pen:45});vm.runInContext("yieldSelected.add('duplicate')",ctx);assert(ctx.yieldLocalCurve('G','THIRD',20,55).error.includes('同一收率'));
ctx.db.yieldObservations=[{id:'bad',type:'cut',grade:'G',batch:'THIRD',yieldPct:57,pen:10}];vm.runInContext("yieldSelected=new Set(['bad'])",ctx);assert(ctx.yieldLocalCurve('G','THIRD',20,55).error.includes('趋势'));
// The explicit historical model is retained, while the new mode never silently falls back to it.
ctx.db.yieldObservations=JSON.parse(before);vm.runInContext('yieldSelected=new Set(db.yieldObservations.map(o=>o.id))',ctx);
set('calcMode','experience');set('calcBatch','NEW');set('calcTarget',68);ctx.calculateYield();assert(resultText.includes('原经验公式'));assert(Math.abs(40.83+ctx.piecewiseYield(42,68)-43.49666666666666)<1e-9);
assert.strictEqual(JSON.stringify(ctx.db.yieldObservations),before);
console.log('Generic feedstock-specific multi-point curves, bidirectional interpolation, no cross-batch fallback and retained historical model passed');
