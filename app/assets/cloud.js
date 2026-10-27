let photoPending=null;
const activePhotoAttempts=new Set();
function photoViscosityUnit(unit){const text=normalizedViscosityUnit(unit);return /^(?:mpa\.?s|cp)$/.test(text)?'mPa·s':/^pa\.?s$/.test(text)?'Pa·s':''}
function editPhotoUnit(groupId,photoId,attemptId,index,unit){const {attempt}=photoAttempt(groupId,photoId,attemptId);if(!attempt)return;attempt.reviewUnits=attempt.reviewUnits||[];attempt.reviewUnits[index]=unit;persistPhotos()}
function canCloudRecognize(run){return isBBR(run)||isAgingMassLoss(run)||/软化点|针入度|延度|延伸度|旋转[黏粘]度|动力[黏粘]度/.test(String(run.name||''))}
function cloudSettingsUI(){
  const ready=!!(window.Android&&Android.hasArkKey&&Android.hasArkKey());
  return `<div class="card"><details><summary>云端识别设置· ${ready?'已保存密钥':'待填写密钥'}</summary><p class="muted">填写火山方舟 API 密钥。密钥加密保存在本机；识别时照片会上传至火山方舟，识别结果需由你核对。</p><input id="arkKey" type="password" autocomplete="off" placeholder="在这里输入新密钥（不会显示原密钥）"><div class="step-actions"><button class="secondary" onclick="saveCloudKey()">保存密钥</button>${ready?'<button class="secondary" onclick="clearCloudKey()">移除密钥</button>':''}</div></details></div>`;
}
function saveCloudKey(){let v=get('arkKey')?.value.trim();if(!v)return alert('请填写新密钥');if(!window.Android||!Android.saveArkKey(v))return alert('密钥保存失败');get('arkKey').value='';render()}
function clearCloudKey(){if(!window.Android)return;Android.clearArkKey();render()}
function cloudReady(){if(window.Android&&Android.hasArkKey&&Android.hasArkKey())return true;alert('请先到“设置”保存 API 密钥');return false}
function cloudExpectedCount(run,group){return isBBR(run)?2:isAgingMassLoss(run)?3:isSoftening(run)?2:/延度|延伸度|针入度|旋转[黏粘]度|动力[黏粘]度/.test(run.name)?Math.max(3,group.readings.length):Math.max(1,group.readings.length)}
function cloudLabelIndex(run,label){
  if(isBBR(run))return bbrCandidateSlot({label});
  let t=String(label||'').replace(/\s/g,'');
  if(isRotationalViscosity(run)&&/^(?:当前|实时)?[黏粘]度(?:值|读数)?$/.test(t))return 0;
  if(/平均|当前|温度|时间|转速|拉力|仪器/.test(t))return -1;
  if(isAgingMassLoss(run)){
    if(/空容器|空瓶|容器空重|空皿|皮重/.test(t))return 0;
    if(/未老化|老化前|初始|加样后|装样后/.test(t))return 1;
    if(/老化后|处理后|最终|终重/.test(t))return 2;
    const numbered=t.match(/(?:读数|质量|第)([123])(?:次|号|组|[:：]|$)/);
    return numbered?Number(numbered[1])-1:-1;
  }
  if(/旋转[黏粘]度/.test(run.name)){
    const numbered=t.match(/(?:第)?([123])(?:次|号|组|[:：]|$)/)||t.match(/(?:黏度|粘度)([123])/);
    if(numbered)return Number(numbered[1])-1;
    if(/旋转[黏粘]度|^(?:黏度|粘度)(?:值|读数)?$/.test(t))return 0;
  }
  if(/动力[黏粘]度/.test(run.name)){
    const numbered=t.match(/(?:第)?([123])(?:次|号|组|[:：]|$)/)||t.match(/(?:动力)?[黏粘]度([123])/);
    if(numbered)return Number(numbered[1])-1;
    if(/动力[黏粘]度|^(?:动力)?[黏粘]度(?:值|读数)?$/.test(t))return 0;
  }
  if(isSoftening(run)){
    if(/上部|软化点[1ＡA]/i.test(t))return 0;
    if(/下部|软化点[2ＢB]/i.test(t))return 1;
  }else if(/针入度|延度|延伸度/.test(run.name)){
    let m=t.match(/(?:第)?([123])(?:次|号|组|[:：]|$)/) || t.match(/(?:针入度|延度|延伸度)([123])/);
    if(m)return Number(m[1])-1;
  }
  return -1;
}
function photoTarget(runId,groupId){const run=db.runs.find(r=>r.id===runId);return {run,group:run?.groups?.find(g=>g.id===groupId)}}
function persistPhotos(){localStorage.setItem(KEY,JSON.stringify(db))}
function photoUrl(id,variant='thumb'){return /^[a-f0-9-]{36}$/.test(id)?`https://asphalt.local/photos/${id}/preview?v=${findExperimentPhoto(id)?.photo.revision||0}`:''}
function preserveVisibleDraft(){if(page==='samples'&&openRun&&get('verdict'))saveResultsDraft(openRun)}
function refreshPhotoPanel(runId,groupId){const {run,group}=photoTarget(runId,groupId),panel=get('photos_'+groupId);if(run&&group&&panel)panel.outerHTML=photoGroupUI(run,group)}
function refreshAfterResume(){if(navigationAnimating||sectionTouch?.drag)return;if(typeof refreshTimers==='function')refreshTimers()}
function takeReadingPhoto(runId,groupId){addExperimentPhoto(runId,groupId,true)}
function chooseReadingPhoto(runId,groupId){addExperimentPhoto(runId,groupId,false)}
function addExperimentPhoto(runId,groupId,camera){const {run}=photoTarget(runId,groupId);if(!run)return;const auto=canCloudRecognize(run)&&!!window.Android?.hasArkKey?.();attachExperimentPhoto(runId,groupId,camera,auto)}
function attachExperimentPhoto(runId,groupId,camera,recognize=false){
  const {run,group}=photoTarget(runId,groupId);if(!run||!group)return;
  if(recognize&&!cloudReady())return;
  if(!window.Android?.attachPhotoForGroup)return alert('请在新版安卓软件中添加照片');
  preserveVisibleDraft();photoPending={runId,groupId,message:'等待照片…'};refreshPhotoPanel(runId,groupId);
  Android.attachPhotoForGroup(runId,groupId,run.name,camera,recognize);
}
window.onNativePhotoEvent=function(event){
  try{
    const {run,group}=photoTarget(event.runId,event.groupId),p=event.payload||{};
    if(run&&group){
      preserveVisibleDraft();
      if(event.type==='attached'){
        group.photos=group.photos||[];
        if(!group.photos.some(x=>x.id===p.id)){
          group.photos.push({id:p.id,createdAt:p.createdAt,photoSaved:!!p.photoSaved,attempts:[]});
          persistPhotos();photoPending=null;
          if(p.autoRecognize&&canCloudRecognize(run))recognizeSavedPhoto(run.id,group.id,p.id);
        }
      }else if(event.type==='result'){
        const photo=group.photos?.find(x=>x.id===p.photoId),attempt=photo?.attempts?.find(x=>x.id===p.attemptId);
        if(attempt&&!attempt.finishedAt){
          const candidates=(Array.isArray(p.readings)?p.readings:[]).filter(x=>x&&typeof x.label==='string'&&/^[-+]?\d+(?:\.\d+)?$/.test(String(x.value))).map(x=>({label:x.label,value:String(x.value),unit:String(x.unit||''),timeSeconds:x.timeSeconds}));
          const values=Array(cloudExpectedCount(run,group)).fill('');
          const units=Array(values.length).fill('');
          const conflicts=new Set();
          for(const x of candidates){const i=isBBR(run)?bbrCandidateSlot(x):cloudLabelIndex(run,x.label);if(isBBR(run)&&i>=0&&values[i]&&Number(values[i])!==Number(x.value))conflicts.add(i);if(i>=0&&i<values.length&&!values[i]){values[i]=x.value;units[i]=isBBR(run)?x.unit:photoViscosityUnit(x.unit)}}
          if(isBBR(run)){for(const i of conflicts)values[i]='';attempt.bbrSpecimenIndex=bbrPhotoTarget(group)}
          if(isRotationalViscosity(run)&&!values.some(Boolean)&&candidates.length===1&&photoViscosityUnit(candidates[0].unit)){values[0]=candidates[0].value;units[0]=photoViscosityUnit(candidates[0].unit)}
          Object.assign(attempt,{finishedAt:Date.now(),status:p.error?'error':'review',error:p.error||'',note:p.note||'',candidates,originalValues:values.slice(),reviewValues:values.slice(),reviewUnits:units});
          activePhotoAttempts.delete(attempt.id);persistPhotos();
        }
      }else if(event.type==='error'){photoPending={runId:run.id,groupId:group.id,message:p.error||'照片操作未完成'};}
      refreshPhotoPanel(run.id,group.id);
    }
    if(window.Android?.acknowledgePhotoEvent)Android.acknowledgePhotoEvent(event.eventId);
  }catch(e){console.error('Photo event could not be saved',e);alert('照片记录尚未保存，请释放手机存储空间后重新打开软件。');}
};
function recognizeSavedPhoto(runId,groupId,photoId){
  const {run,group}=photoTarget(runId,groupId),photo=group?.photos?.find(p=>p.id===photoId);
  if(!run||!photo||!canCloudRecognize(run)||!cloudReady())return;
  if(photo.attempts?.some(a=>activePhotoAttempts.has(a.id)))return;
  preserveVisibleDraft();const attempt={id:uid(),createdAt:Date.now(),status:'loading'};
  (photo.attempts=photo.attempts||[]).push(attempt);persistPhotos();activePhotoAttempts.add(attempt.id);refreshPhotoPanel(runId,groupId);
  try{Android.recognizeAttachment(runId,groupId,run.name,photoId,attempt.id)}catch(e){attempt.status='error';attempt.error='识别未能启动，请点击重试';activePhotoAttempts.delete(attempt.id);persistPhotos();refreshPhotoPanel(runId,groupId)}
}
function photoAttempt(groupId,photoId,attemptId){for(const run of db.runs){const group=run.groups?.find(g=>g.id===groupId),photo=group?.photos?.find(p=>p.id===photoId),attempt=photo?.attempts?.find(a=>a.id===attemptId);if(attempt)return {run,group,photo,attempt}}return {}}
function editPhotoReview(groupId,photoId,attemptId,index,value){const {attempt}=photoAttempt(groupId,photoId,attemptId);if(!attempt)return;attempt.reviewValues[index]=value;persistPhotos()}
function choosePhotoCandidate(groupId,photoId,attemptId,index,select){if(select.value==='')return;const {run,attempt}=photoAttempt(groupId,photoId,attemptId),candidate=attempt?.candidates?.[Number(select.value)];if(!candidate)return;if(isBBR(run)&&bbrCandidateSlot(candidate)!==index)return alert('请选择明确标注 60 s 且对应 S 或 m 的候选值');const input=get('photoValue_'+attemptId+'_'+index);if(input)input.value=candidate.value;editPhotoReview(groupId,photoId,attemptId,index,candidate.value);if(isBBR(run)){editPhotoUnit(groupId,photoId,attemptId,index,candidate.unit);refreshPhotoPanel(run.id,groupId)}if(isRotationalViscosity(run)){editPhotoUnit(groupId,photoId,attemptId,index,photoViscosityUnit(candidate.unit));refreshPhotoPanel(run.id,groupId)}}
function applyPhotoReadings(groupId,photoId,attemptId){
  const {run,group,attempt}=photoAttempt(groupId,photoId,attemptId);if(!attempt||attempt.status!=='review')return;
  const values=attempt.reviewValues.map(v=>String(v).trim());
  if(!values.some(Boolean))return alert('没有可填入的读数，可手填或重新识别');
  if(values.some(v=>v!==''&&!/^[-+]?\d+(?:\.\d+)?$/.test(v)))return alert('请核对读数格式');
  if(isBBR(run)){
    const target=bbrSpecimenMode(group)?Number(attempt.bbrSpecimenIndex):0;
    if(!Number.isInteger(target)||target<0||target>(bbrSpecimenMode(group)?2:0))return alert('请选择试件');
    const unit=bbrPhotoStiffnessUnit(attempt.reviewUnits?.[0]);
    if(values[0]&&!unit)return alert('请核对 S 的单位，选择 MPa 或 kPa');
    if(values[1]&&!['','—','-','1','无量纲','dimensionless'].includes(String(attempt.reviewUnits?.[1]||'').trim().toLowerCase()))return alert('m 为无量纲值，请核对照片单位；不能直接填入百分数');
    const applied=values.map((v,i)=>i===0&&v&&unit==='kPa'?String(Number(v)/1000):v);
    if(applied[0]&&Number(applied[0])<=0||applied[1]&&Number(applied[1])<0)return alert('S 应大于 0，m 不应为负值');
    preserveVisibleDraft();const offset=target*2;
    if(applied.some((v,i)=>v!==''&&String(group.readings[offset+i]??'')!==''&&Number(group.readings[offset+i])!==Number(v))&&!confirm('这次填入将替换该试件已有的 S 或 m，确定吗？'))return;
    attempt.beforeValues=group.readings.slice();
    while(group.readings.length<(bbrSpecimenMode(group)?6:2))group.readings.push('');
    applied.slice(0,2).forEach((v,i)=>{if(v!=='')group.readings[offset+i]=v});
    attempt.appliedValues=applied;attempt.appliedUnit='S: MPa；m: —';attempt.appliedAt=Date.now();attempt.status='applied';
    group.readingHistory=group.readingHistory||[];group.readingHistory.push({at:Date.now(),source:'photo',photoId,attemptId,values:group.readings.slice()});
    if(resultsComplete(run))completeRun(run);persistPhotos();render();return;
  }
  const rotational=isRotationalViscosity(run);
  if(rotational&&values.some((v,i)=>v!==''&&!photoViscosityUnit(attempt.reviewUnits?.[i])))return alert('请核对每个旋转粘度读数的单位：仪器显示 mPa.s 时请选择 mPa·s');
  const applied=values.map((v,i)=>v!==''&&rotational?String(Number(v)/(photoViscosityUnit(attempt.reviewUnits[i])==='mPa·s'?1000:1)):v);
  preserveVisibleDraft();
  if(applied.some((v,i)=>v!==''&&String(group.readings[i]??'')!==''&&Number(rotationValueInPa(run,group.readings[i]))!==Number(v))&&!confirm('这次填入将替换本组对应的已有读数，确定吗？'))return;
  if(rotational){normalizeRotationRun(run);run.unit='Pa·s';run.rotationInputUnit='Pa·s'}
  attempt.beforeValues=group.readings.slice();while(group.readings.length<values.length)group.readings.push('');
  applied.forEach((v,i)=>{if(v!=='')group.readings[i]=v});
  attempt.appliedValues=applied.slice();attempt.appliedUnit=rotational?'Pa·s':run.unit;attempt.appliedAt=Date.now();attempt.status='applied';
  group.readingHistory=group.readingHistory||[];group.readingHistory.push({at:Date.now(),source:'photo',photoId,attemptId,values:group.readings.slice()});
  if(resultsComplete(run)){normalizeRotationRun(run);completeRun(run)}
  persistPhotos();render();
}
function recordManualReadingHistory(run){for(const group of run.groups||[]){if(!group.photos?.length)continue;const h=group.readingHistory=group.readingHistory||[],last=h[h.length-1];if(!last||JSON.stringify(last.values)!==JSON.stringify(group.readings))h.push({at:Date.now(),source:'manual',values:group.readings.slice()})}}
function readingText(values){return (values||[]).map(v=>String(v).trim()||'—').join(' / ')}
function photoReviewHTML(run,group,photo,a){
  if(!a)return '';
  if(a.status==='loading')return '<p class="photo-state" role="status">正在上传识别…照片已保存在本机</p>';
  if(a.status==='error')return `<p class="photo-error">${esc(a.error)}</p>`;
  if(a.status!=='review')return '';
  const labels=isBBR(run)?['S(60 s) · MPa','m(60 s) · 无量纲']:isAgingMassLoss(run)?AGING_MASS_LABELS:isSoftening(run)?(isSegregation(run,group)?['上部软化点','下部软化点']:['软化点 1 / A','软化点 2 / B']):/旋转[黏粘]度/.test(run.name)?['旋转黏度 1','旋转黏度 2','旋转黏度 3']:/动力[黏粘]度/.test(run.name)?['动力黏度 1','动力黏度 2','动力黏度 3']:['读数 1','读数 2','读数 3'];
  return `<div class="photo-review"><strong>核对后填入</strong>${isBBR(run)&&bbrSpecimenMode(group)?`<label>填入试件<select aria-label="BBR照片目标试件" onchange="editBBRPhotoTarget('${group.id}','${photo.id}','${a.id}',this.value)">${[0,1,2].map(n=>`<option value="${n}" ${Number(a.bbrSpecimenIndex)===n?'selected':''}>试件 ${n+1}</option>`).join('')}</select></label>`:''}<p class="muted">${isBBR(run)?'仅填入 60 s 的 S、m，请核对时间点和试件；其他时间点不会自动选中。':''}请核对小数点与标签，保留原始读数。${isRotationalViscosity(run)?'单位按照片核对：mPa.s = mPa·s，除以 1000 后保存为 Pa·s。单位不清楚时请手动选择。':''}${esc(a.note||'')}${!a.candidates.length?'未识别出读数，可手填或重新识别。':''}</p>${a.reviewValues.map((v,i)=>`<label for="photoValue_${a.id}_${i}">${esc(labels[i]||'读数 '+(i+1))}</label><div class="photo-review-row"><input id="photoValue_${a.id}_${i}" type="number" step="any" value="${esc(v)}" oninput="editPhotoReview('${group.id}','${photo.id}','${a.id}',${i},this.value)"><select aria-label="候选读数" onchange="choosePhotoCandidate('${group.id}','${photo.id}','${a.id}',${i},this)"><option value="">选择候选值</option>${a.candidates.map((c,ci)=>`<option value="${ci}">${esc(c.label)}：${esc(c.value)} ${esc(c.unit||'')}</option>`).join('')}</select></div>${isBBR(run)&&i===0?`<label>S 的照片单位<select aria-label="BBR S 照片单位" onchange="editPhotoUnit('${group.id}','${photo.id}','${a.id}',0,this.value)"><option value="">请选择单位</option>${['MPa','kPa'].map(u=>`<option value="${u}" ${bbrPhotoStiffnessUnit(a.reviewUnits?.[0])===u?'selected':''}>${u}</option>`).join('')}</select></label>`:''}${isRotationalViscosity(run)?`<label>照片读数单位<select aria-label="照片读数单位" onchange="editPhotoUnit('${group.id}','${photo.id}','${a.id}',${i},this.value)"><option value="">请选择单位</option><option value="mPa·s" ${a.reviewUnits?.[i]==='mPa·s'?'selected':''}>mPa·s（mPa.s，除以 1000）</option><option value="Pa·s" ${a.reviewUnits?.[i]==='Pa·s'?'selected':''}>Pa·s（无需换算）</option></select></label>`:''}`).join('')}<div class="step-actions"><button onclick="applyPhotoReadings('${group.id}','${photo.id}','${a.id}')">核对后填入</button></div></div>`;
}
function photoGroupUI(run,group){
  const photos=group.photos||[],can=canCloudRecognize(run),pending=photoPending?.runId===run.id&&photoPending?.groupId===group.id?photoPending.message:'';
  return `<section class="photo-panel" id="photos_${group.id}"><div class="photo-heading"><strong>仪器照片</strong><small>${photos.length?photos.length+' 张':'可留存原始结果'}</small></div><div class="photo-tools"><button class="secondary" onclick="takeReadingPhoto('${run.id}','${group.id}')">拍照</button><button class="secondary" onclick="chooseReadingPhoto('${run.id}','${group.id}')">相册</button></div>${pending?`<p class="photo-state">${esc(pending)}</p>`:''}${photos.slice().reverse().map((p,i)=>{const a=p.attempts?.[p.attempts.length-1];return `<article class="photo-record"><details class="photo-options"><summary aria-label="照片 ${photos.length-i} 选项">⋮</summary><div><button class="secondary" onclick="rotateExperimentPhoto('${p.id}')">向右旋转 90°</button><button class="secondary" onclick="deleteExperimentPhoto('${p.id}')">删除这张照片</button></div></details><div class="photo-overview"><button class="photo-thumb" aria-label="查看仪器照片 ${photos.length-i}" onclick="openExperimentPhoto('${p.id}')"><img loading="lazy" src="${photoUrl(p.id)}" alt="查看照片" onerror="this.style.display='none';this.nextElementSibling.hidden=false"><span hidden>照片不可用</span></button><div><strong>照片 ${photos.length-i}</strong><small>${esc(stamp(p.createdAt))}</small><small>${p.photoSaved?'已保存到相册并关联本组':'已关联本组'}</small></div></div>${photoReviewHTML(run,group,p,a)}${can?`<div class="step-actions"><button class="secondary" ${a?.status==='loading'?'disabled':''} onclick="recognizeSavedPhoto('${run.id}','${group.id}','${p.id}')">${a?.status==='loading'?'识别中':a?.status==='error'?'重试识别（同一照片）':a?'重新识别':'识别这张照片'}</button></div>`:''}${p.attempts?.length?`<details class="photo-audit"><summary>识别与核对记录</summary>${p.attempts.slice().reverse().map(x=>`<div><small>${esc(stamp(x.createdAt))}</small>${x.error?`<p>${esc(x.error)}</p>`:`<p>识别原值：${esc((x.candidates||[]).map(c=>c.label+' '+c.value+' '+(c.unit||'')).join('；')||'暂无')}</p>${x.appliedAt?`<p>核对填入：${esc(readingText(x.appliedValues))} ${esc(x.appliedUnit||'')}</p>`:'<p>尚未填入</p>'}`}</div>`).join('')}</details>`:''}</article>`}).join('')}${group.readingHistory?.length?`<details class="photo-audit"><summary>读数保存记录</summary><p>当前读数：${esc(readingText(group.readings))}</p>${group.readingHistory.slice().reverse().map(h=>`<p>${esc(stamp(h.at))} · ${h.source==='photo'?'照片核对填入':'手动保存'}<br>${esc(readingText(h.values))}</p>`).join('')}</details>`:''}</section>`;
}
function findExperimentPhoto(id){for(const run of db.runs)for(const group of run.groups||[]){const photo=group.photos?.find(p=>p.id===id);if(photo)return {run,group,photo}}}
function photoNativeOperation(method,id){
  try{if(!window.Android?.[method])throw Error('请安装新版软件后操作');const result=JSON.parse(Android[method](id));if(!result.ok)throw Error(result.error||'照片操作失败');return true}catch(e){alert(e.message);return false}
}
function rotateExperimentPhoto(id){
  const target=findExperimentPhoto(id);if(!target)return;
  if(target.photo.attempts?.some(a=>activePhotoAttempts.has(a.id)))return alert('这张照片正在识别，请识别结束后旋转');
  preserveVisibleDraft();if(!photoNativeOperation('rotateExperimentPhoto',id))return;
  target.photo.revision=(target.photo.revision||0)+1;persistPhotos();refreshPhotoPanel(target.run.id,target.group.id);
  const viewer=get('photoViewer');if(viewer?.dataset.photoId===id)viewer.querySelector('img').src=photoUrl(id);
}
function deleteExperimentPhoto(id){
  const target=findExperimentPhoto(id);if(!target)return;
  if(!confirm('删除这张照片附件及其识别记录？已填写的数据、读数保存记录和相册原图会保留。'))return;
  preserveVisibleDraft();
  // Commit association removal first so a late recognition callback cannot restore it.
  const before=target.group.photos;target.group.photos=before.filter(p=>p.id!==id);
  try{persistPhotos()}catch(e){target.group.photos=before;return alert('删除未保存，请释放存储空间后重试')}
  for(const a of target.photo.attempts||[])activePhotoAttempts.delete(a.id);
  if(get('photoViewer')?.dataset.photoId===id)closeExperimentPhoto();
  refreshPhotoPanel(target.run.id,target.group.id);
  photoNativeOperation('deleteExperimentPhoto',id);
}
function openExperimentPhoto(id){if(!photoUrl(id))return;preserveVisibleDraft();closeExperimentPhoto();const overlay=document.createElement('div');overlay.id='photoViewer';overlay.dataset.photoId=id;overlay.className='photo-viewer';overlay.setAttribute('role','dialog');overlay.setAttribute('aria-modal','true');overlay.setAttribute('aria-label','仪器照片');overlay.innerHTML=`<div class="photo-viewer-bar"><span>仪器照片</span><button onclick="rotateExperimentPhoto('${id}')">旋转</button><button onclick="deleteExperimentPhoto('${id}')">删除</button><button onclick="get('photoViewer').classList.toggle('zoomed')">放大 / 还原</button><button onclick="closeExperimentPhoto()">关闭</button></div><div class="photo-viewer-image"><img src="${photoUrl(id,'preview')}" alt="仪器照片" onerror="this.alt='本机找不到该照片，请重新添加'"></div>`;document.body.append(overlay);document.body.style.overflow='hidden'}
function closeExperimentPhoto(){const viewer=get('photoViewer');if(!viewer)return false;viewer.remove();document.body.style.overflow='';return true}
function initPhotoRecords(){let changed=false;for(const r of db.runs)for(const g of r.groups||[])for(const p of g.photos||[])for(const a of p.attempts||[])if(a.status==='loading'){a.status='error';a.error='上次识别未完成，可使用这张照片重试';changed=true}if(changed)persistPhotos()}
