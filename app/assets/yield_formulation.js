const YIELD_TOTAL_FEED='mixed-feed-total';
function yieldFormulation(){
  const blendOil=yieldText('calcBlendOil'),raw=yieldNum('calcMotherPct');
  if(!yieldValid(raw,.000001,100)||raw!==100&&!blendOil)return null;
  return {blendOil:raw===100?'':blendOil,motherPct:Math.round(raw*1e6)/1e6,yieldBasis:YIELD_TOTAL_FEED};
}
function yieldHasFormulation(record){return record?.yieldBasis===YIELD_TOTAL_FEED&&yieldValid(Number(record.motherPct),.000001,100)&&(Number(record.motherPct)===100||String(record.blendOil||'').trim()!=='')}
function yieldSameFormulation(record,formulation){return yieldHasFormulation(record)&&Math.abs(Number(record.motherPct)-formulation.motherPct)<1e-7&&(formulation.motherPct===100||String(record.blendOil).trim()===formulation.blendOil)}
function yieldFormulationLabel(record){return !yieldHasFormulation(record)?'配料条件／收率口径待补充':Number(record.motherPct)===100?'纯母油 100% · 调配油 0%；收率按母油投料质量':`调配油 ${record.blendOil}；母油∶调配油 = ${yieldFmt(Number(record.motherPct))}∶${yieldFmt(100-Number(record.motherPct))}；收率按总投料`}
function syncYieldBlendField(){const field=get('calcBlendOil');if(!field)return;field.disabled=yieldNum('calcMotherPct')===100;field.placeholder=field.disabled?'纯母油，无需填写调配油':'填写调配油名称或批次'}
function tagYieldObservation(id){
  captureYieldDraft();const record=db.yieldObservations.find(o=>o.id===id),formulation=yieldFormulation();if(!record||record.type!=='cut')return;
  if(record.grade!==yieldGrade||String(record.batch||'').trim()!==yieldText('calcBatch'))return alert('请先填写这条记录所属的牌号和母油批次，再补充配料条件。');
  if(!formulation)return alert('先填写调配油标识和母油质量占比。');
  if(!confirm(`请确认这条收率已按母油＋调配油的总投料质量计算，且实际配料为：\n${yieldFormulationLabel(formulation)}\n仅给这条记录补充条件，原收率与针入度不变。`))return;
  record.formulationHistory=record.formulationHistory||[];
  record.formulationHistory.push({blendOil:record.blendOil,motherPct:record.motherPct,yieldBasis:record.yieldBasis,changedAt:Date.now()});
  Object.assign(record,formulation);yieldSelected.add(id);save();refreshYieldPrediction();
}
