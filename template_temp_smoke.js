const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(path.join(__dirname,'app/assets/index.html'),'utf8');
function part(start,end){const i=html.indexOf(start),j=html.indexOf(end,i);assert(i>=0&&j>i,start);return html.slice(i,j)}
const source=[
  part('function templateValueOptions(', 'function closeRunOptions('),
  part('function ensureRunTemperatures(', 'function ensureAgingMassReadings('),
].join('\n');
const db={runs:[{temp:'60 ℃'},{temp:'60.0'},{temp:''}]};
const ctx={db,KEY:'smoke',localStorage:{setItem:()=>{}},String,Number,Set};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/bbr.js','utf8'),ctx);vm.runInContext(source,ctx);
assert.deepStrictEqual(db.runs.map(r=>r.temp),['60','60','']);
const one={name:'动力粘度',standard:'T 0620',temps:'60℃'};
assert.strictEqual(ctx.defaultRunOptions(one).temp,'60');
assert.strictEqual(ctx.hasRunOptions(one),false);
const named={name:'60 ℃动力黏度',standard:'T 0620',temps:'60℃、135℃'};
assert.strictEqual(ctx.defaultRunOptions(named).temp,'60');
assert.strictEqual(ctx.needsTemperatureChoice(named),false);
assert.strictEqual(ctx.hasRunOptions(named),false);
const ambiguous={name:'动力黏度',standard:'T 0620',temps:'60℃,135℃'};
assert.strictEqual(ctx.needsTemperatureChoice(ambiguous),true);
assert.strictEqual(ctx.hasRunOptions(ambiguous),true);
const multipleStandards={name:'动力黏度',standard:'T 0620,ASTM D2171',temps:'60℃'};
assert.strictEqual(ctx.hasRunOptions(multipleStandards),true);
assert.strictEqual(ctx.needsTemperatureChoice(multipleStandards),false);
assert.strictEqual(ctx.defaultRunOptions({name:'动力黏度',standard:'T 0620',temps:''}).temp,'');
for(const [name,temps,expected,asks] of [
  ['针入度（25℃）','25,15,30','25',false],
  ['软化点','5℃','5',false],
  ['135℃旋转黏度','135℃','135',false],
  ['延度','10℃,15℃','10',true],
  ['蜡含量','','',false],
]){
  const template={name,temps,standard:'单一规范'};
  assert.strictEqual(ctx.defaultRunOptions(template).temp,expected,name);
  assert.strictEqual(ctx.needsTemperatureChoice(template),asks,name);
}
console.log('template temperature defaults and legacy values passed');
