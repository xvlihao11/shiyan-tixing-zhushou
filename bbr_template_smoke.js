const fs=require('fs'),vm=require('vm'),assert=require('assert');
const html=fs.readFileSync(__dirname+'/app/assets/index.html','utf8');
function part(a,b){const i=html.indexOf(a),j=html.indexOf(b,i);assert(i>=0&&j>i,a);return html.slice(i,j)}
const legacy={templates:[{id:'old',name:'低温弯曲蠕变劲度 S',standard:'原标准',steps:[{name:'旧步骤',minutes:3,mode:'none'}]}],samples:[{id:'s',name:'试样',sampleCount:1}],runs:[{id:'oldrun',name:'已有试验',steps:[{started:123,minutes:60}],groups:[{readings:['123']}]}],projectGroups:[]};
let stored=JSON.stringify(legacy),renders=0;const fields=new Map();
const ctx={localStorage:{getItem:()=>stored,setItem:(k,v)=>stored=v},window:{},document:{getElementById:id=>fields.get(id)},projectGroupEditor:()=>'',alert(){},confirm:()=>true};
vm.createContext(ctx);vm.runInContext(fs.readFileSync(__dirname+'/app/assets/bbr.js','utf8'),ctx);
// Execute the actual startup sequence against an existing user's database.
vm.runInContext(part('const KEY=', 'const esc=')+part('function templateValueOptions(', 'function normalizeRunTemperature('),ctx);
vm.runInContext("function render(){renderCalls=(typeof renderCalls==='undefined'?0:renderCalls)+1};const get=id=>document.getElementById(id);const esc=x=>String(x??'');",ctx);
vm.runInContext(part('function templatesUI(', 'function settingsUI('),ctx);
vm.runInContext(part('function collectTemplateForm(', 'const md=x=>'),ctx);
vm.runInContext(part('function normalizeRunTemperature(', 'function closeRunOptions('),ctx);
vm.runInContext(part(' function addRun(', 'function toggleRunMenu('),ctx);
vm.runInContext("function sampleChoices(s){return [s.name]};function save(){localStorage.setItem(KEY,JSON.stringify(db));render()}",ctx);
const data=()=>JSON.parse(vm.runInContext('JSON.stringify(db)',ctx));
let template=data().templates.find(t=>t.resultKind==='bbr60');assert(template);assert.strictEqual(data().templates.filter(t=>t.resultKind==='bbr60').length,1);
assert.deepStrictEqual(data().runs,legacy.runs);const old=data().templates.find(t=>t.id==='old');assert.deepStrictEqual(old.steps,legacy.templates[0].steps);assert(old.standard.startsWith('原标准'));assert.strictEqual(old.name,legacy.templates[0].name);
ctx.openBBRTemplate();assert.strictEqual(vm.runInContext('openTemplateId',ctx),template.id);
let ui=ctx.templatesUI();for(const id of ['tn','ts','tt','stepName_','stepMinutes_','stepMode_'])assert(ui.includes('id="'+id+template.id));
assert(ui.includes('BBR低温试验设置'));assert(ui.includes('独立 S / m 读数框'));
const set=(id,value)=>fields.set(id,{value});
set('tn'+template.id,'我的低温试验');set('ts'+template.id,'本单位采用规范');set('tr'+template.id,'yes');set('tu'+template.id,'S: MPa；m: —');set('tt'+template.id,'-18');
template.steps.forEach((step,i)=>{set(`stepName_${template.id}_${i}`,step.name);set(`stepMinutes_${template.id}_${i}`,i===1?'60':'0');set(`stepMode_${template.id}_${i}`,i===1?'alarm':'none')});
ctx.saveTemplate(template.id);template=data().templates.find(t=>t.id===template.id);assert.strictEqual(template.temps,'-18');assert.strictEqual(template.standard,'本单位采用规范');assert.strictEqual(template.steps[1].minutes,60);assert.strictEqual(template.steps[1].mode,'alarm');
ctx.templatesUI();assert.strictEqual(data().templates.filter(t=>t.resultKind==='bbr60').length,1);assert(ctx.isBBR(template));
ctx.addRun('s',template.id,false);let run=data().runs.at(-1);assert.strictEqual(run.resultKind,'bbr60');assert.strictEqual(run.temp,'-18');assert.strictEqual(run.standard,'本单位采用规范');assert.strictEqual(run.groups[0].readings.length,2);assert.strictEqual(run.steps[1].mode,'alarm');assert(ctx.isBBR(run));
// Removing a template while keeping its experiments is repaired on entering the editor.
vm.runInContext('db.templates=db.templates.filter(t=>t.resultKind!=="bbr60")',ctx);ctx.templatesUI();assert(data().templates.some(t=>t.resultKind==='bbr60'));assert.strictEqual(data().runs.length,2);
console.log('BBR real startup migration, visible editor, saved options and new-run inheritance passed');
