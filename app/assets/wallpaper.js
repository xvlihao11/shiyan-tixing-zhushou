// The wallpaper gallery is private to this app. Preferences never enter lab records.
let wallpaperKind='',wallpaperRevision=0,wallpaperTransparency=0,wallpaperSource='direct';
let wallpaperGallery=[],wallpaperActiveId='',wallpaperSectionProgress=0;
let wallpaperMenuId=null,wallpaperThumbnailsReady=false;
let wallpaperAppBackground=false,wallpaperInitialized=false,wallpaperMediaUrl='';
let cardTransparency=63,wallpaperPlaybackError='',wallpaperBlobUrl='',wallpaperLoadToken=0;
const wallpaperFrameKey='ui_trial_wallpaper_frame_v546';
const wallpaperFramesKey='ui_wallpaper_frames_v545';
let wallpaperFrame={fit:'height',zoom:100,x:50,y:50},wallpaperFrames={};
function normalizeWallpaperFrame(saved={}){
  const bounded=(key,min,max,fallback)=>Number.isFinite(Number(saved[key]))?Math.max(min,Math.min(max,Number(saved[key]))):fallback;
  return {fit:['height','cover','contain'].includes(saved.fit)?saved.fit:'height',zoom:bounded('zoom',50,250,100),x:bounded('x',0,100,50),y:bounded('y',0,100,50)};
}
try{
  const saved=JSON.parse(localStorage.getItem(wallpaperFrameKey)||'{}');
  wallpaperFrames=JSON.parse(localStorage.getItem(wallpaperFramesKey)||'{}');
  wallpaperFrame=normalizeWallpaperFrame(saved);
}catch(_){}
try{
  if(localStorage.getItem('ui_trial_wallpaper_vivid_v543')==='1')wallpaperTransparency=Math.max(0,Math.min(95,Number(localStorage.getItem('ui_trial_wallpaper_transparency')??0)));
  else{localStorage.setItem('ui_trial_wallpaper_transparency','0');localStorage.setItem('ui_trial_wallpaper_vivid_v543','1')}
}catch(_){wallpaperTransparency=0}
try{
  const saved=localStorage.getItem('ui_trial_card_transparency');
  if(localStorage.getItem('ui_trial_clear_glass_v548')==='1')cardTransparency=Math.max(45,Math.min(100,Number(saved??63)));
  else{
    cardTransparency=saved===null?63:Math.max(45,Math.min(100,Math.round(50+Number(saved)*.65)));
    localStorage.setItem('ui_trial_card_transparency',String(cardTransparency));
    localStorage.setItem('ui_trial_clear_glass_v548','1');
  }
}catch(_){cardTransparency=63}
function initWallpaper(){
  if(wallpaperInitialized)return;wallpaperInitialized=true;
  if(window.Android?.wallpaperState){try{useWallpaperState(JSON.parse(Android.wallpaperState()))}catch(_){}}
  applyCardTransparency();initWallpaperSliders();
  applyWallpaper();document.addEventListener('visibilitychange',syncWallpaperPlayback);
  window.addEventListener('pageshow',syncWallpaperPlayback);
  window.addEventListener('resize',()=>{wallpaperViewport=null;applyWallpaperFrame()});
  window.addEventListener('pagehide',flushWallpaperPreferences);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)flushWallpaperPreferences()});
}
function applyCardTransparency(){
  // At 100%, both tint and blur layers are fully invisible. Keep the
  // intermediate values linear so 99% to 100% is a small, smooth step.
  const fill=Math.max(0,Math.min(1,(100-cardTransparency)/55));
  if(cardPaintedOpacity===fill)return;cardPaintedOpacity=fill;
  document.documentElement.style.setProperty('--ui-glass-fill',String(fill));
}
function applyWallpaper(){
  let layer=document.getElementById('trialWallpaper');
  if(!layer){layer=document.createElement('div');layer.id='trialWallpaper';layer.className='trial-wallpaper';layer.setAttribute('aria-hidden','true');document.body.prepend(layer)}
  document.body.classList.toggle('has-wallpaper',!!wallpaperKind);
  layer.style.opacity=wallpaperKind?String(1-wallpaperTransparency/100):'0';
  const token=++wallpaperLoadToken,id=encodeURIComponent(wallpaperActiveId);
  if(!wallpaperKind){layer.querySelector('video')?.pause();layer.replaceChildren();if(wallpaperBlobUrl){URL.revokeObjectURL(wallpaperBlobUrl);wallpaperBlobUrl=''}syncWallpaperControls();return}
  const url='https://asphalt.local/wallpaper/item/'+id+'?v='+wallpaperRevision;
  // Video loading never displays a previous image or a thumbnail placeholder.
  if(wallpaperKind==='video'){showWallpaperMedia(layer,wallpaperMediaUrl||url,null);return}
  // The current image stays visible until the replacement can cover the screen.
  const preview=new Image();preview.onload=()=>{if(token===wallpaperLoadToken)showWallpaperMedia(layer,url,preview)};
  preview.onerror=()=>{if(token!==wallpaperLoadToken)return;if(wallpaperKind==='video')showWallpaperMedia(layer,url,null);else{wallpaperPlaybackError='壁纸图片无法读取';syncWallpaperControls()}};
  preview.src=url;
  syncWallpaperControls();
}
function showWallpaperMedia(layer,url,preview){
  const oldVideo=layer.querySelector('video');if(oldVideo)oldVideo.pause();
  if(wallpaperBlobUrl){URL.revokeObjectURL(wallpaperBlobUrl);wallpaperBlobUrl=''}wallpaperPlaybackError='';
  if(wallpaperKind==='image'){
    layer.replaceChildren(preview);applyWallpaperFrame();syncWallpaperControls();return;
  }
  wallpaperThumbnailsReady=false;
  const video=document.createElement('video');video.muted=true;video.loop=true;video.autoplay=true;video.playsInline=true;video.preload='metadata';
  layer.replaceChildren(video);applyWallpaperFrame();
  video.addEventListener('loadedmetadata',applyWallpaperFrame);
  video.addEventListener('loadeddata',syncWallpaperPlayback);
  video.addEventListener('canplay',syncWallpaperPlayback);
  video.addEventListener('error',()=>{if(!video.isConnected)return;if(video.src.startsWith('file:')&&!video.dataset.directFallback){video.dataset.directFallback='1';video.src=video.dataset.legacySrc;video.load();syncWallpaperPlayback();return}if(video.dataset.fallback){wallpaperPlaybackError='当前视频无法解码';syncWallpaperControls()}else videoFallback(video,video.dataset.legacySrc)});
  video.addEventListener('playing',()=>{if(!video.isConnected)return;wallpaperThumbnailsReady=true;wallpaperPlaybackError='';syncWallpaperControls()});
  video.dataset.legacySrc='https://asphalt.local/wallpaper/item/'+encodeURIComponent(wallpaperActiveId)+'?v='+wallpaperRevision;
  video.src=url;
  syncWallpaperPlayback();syncWallpaperControls();

}
async function videoFallback(video,url){
  if(video.dataset.fallback||!video.isConnected)return;video.dataset.fallback='1';
  try{
    const response=await fetch(url,{cache:'no-store'});if(!response.ok)throw Error('无法读取视频');
    const size=Number(response.headers.get('Content-Length')||0);if(size>64*1024*1024)throw Error('视频超过备用播放上限 64 MB');
    const blob=await response.blob();if(blob.size>64*1024*1024)throw Error('视频超过备用播放上限 64 MB');
    if(!video.isConnected||wallpaperKind!=='video')return;
    wallpaperBlobUrl=URL.createObjectURL(blob);video.src=wallpaperBlobUrl;video.load();syncWallpaperPlayback();
  }catch(e){if(!video.isConnected)return;wallpaperPlaybackError=e.message.includes('64 MB')?'视频已保存，但当前播放内核未能直接读取大文件；建议换短一点的视频。':'视频无法播放；可尝试重新播放或换一个视频编码。';syncWallpaperControls()}
}
function wallpaperPlaybackAllowed(){return !wallpaperAppBackground&&!document.hidden&&!window.matchMedia?.('(prefers-reduced-motion: reduce)').matches}
function syncWallpaperPlayback(){
  const video=document.querySelector('#trialWallpaper video');if(!video)return;
  if(!wallpaperPlaybackAllowed()){video.pause();return}
  video.play().catch(e=>{
    if(!video.isConnected||!wallpaperPlaybackAllowed()||e?.name==='AbortError')return;
    if(video.error&&!video.dataset.fallback&&!video.src.startsWith('file:'))videoFallback(video,video.dataset.legacySrc);
    else if(video.readyState>=2){wallpaperPlaybackError='视频未能自动播放，点“重新播放”重试。';syncWallpaperControls()}
  });
}
function retryWallpaperVideo(){
  const video=document.querySelector('#trialWallpaper video');if(!video)return;
  wallpaperPlaybackError='';syncWallpaperControls();
  if(video.error||video.readyState===0)video.load();
  syncWallpaperPlayback();
}
window.setWallpaperAppBackground=function(background){
  wallpaperAppBackground=!!background;syncWallpaperPlayback();
};
function flushWallpaperPreferences(){if(!wallpaperPreferencesTimer)return;clearTimeout(wallpaperPreferencesTimer);wallpaperPreferencesTimer=0;localStorage.setItem('ui_trial_wallpaper_transparency',String(wallpaperTransparency));localStorage.setItem('ui_trial_card_transparency',String(cardTransparency));saveWallpaperFrame()}
function queueWallpaperAppearance(){
 clearTimeout(wallpaperPreferencesTimer);wallpaperPreferencesTimer=setTimeout(flushWallpaperPreferences,180);
 if(wallpaperAppearanceFrame)return;
 wallpaperAppearanceFrame=requestAnimationFrame(()=>{wallpaperAppearanceFrame=0;applyCardTransparency();applyWallpaperFrame();const layer=document.getElementById('trialWallpaper'),opacity=wallpaperKind?String(1-wallpaperTransparency/100):'0';if(layer&&layer.style.opacity!==opacity)layer.style.opacity=opacity;syncWallpaperControls(false)});
}
function queueCardAppearance(){
 clearTimeout(wallpaperPreferencesTimer);wallpaperPreferencesTimer=setTimeout(flushWallpaperPreferences,180);
 if(cardAppearanceFrame)return;
 cardAppearanceFrame=requestAnimationFrame(()=>{
  cardAppearanceFrame=0;applyCardTransparency();
  for(const suffix of ['', 'Settings']){
   const label=document.getElementById('cardTransparencyValue'+suffix),input=document.getElementById('cardTransparency'+suffix);
   if(label&&label.textContent!==cardTransparency+'%')label.textContent=cardTransparency+'%';
   if(input&&Number(input.value)!==cardTransparency){input.value=String(cardTransparency);syncWallpaperSlider(input)}
  }
 });
}
function setWallpaperTransparency(value){wallpaperTransparency=Math.max(0,Math.min(95,Number(value)||0));queueWallpaperAppearance()}
function setCardTransparency(value){const next=Number(value);cardTransparency=Math.max(45,Math.min(100,Number.isFinite(next)?next:63));queueCardAppearance()}
function applyWallpaperFrame(){
  const media=document.querySelector('#trialWallpaper video')||document.querySelector('#trialWallpaper img');if(!media)return;
  const layer=document.getElementById('trialWallpaper');if(!wallpaperViewport)wallpaperViewport={width:layer?.clientWidth||window.innerWidth||390,height:layer?.clientHeight||window.innerHeight||800};const w=wallpaperViewport.width,h=wallpaperViewport.height;
  const naturalW=media.videoWidth||media.naturalWidth||0,naturalH=media.videoHeight||media.naturalHeight||0;
  const ratio=naturalH?naturalW/naturalH:w/h;
  const fit=wallpaperFrame.fit==='height'?(ratio<w/h?'contain':'cover'):wallpaperFrame.fit;
  const baseW=fit==='contain'?Math.min(w,h*ratio):Math.max(w,h*ratio),baseH=baseW/ratio,scale=wallpaperFrame.zoom/100;
  const cropped=baseW*scale>w+2;
  const pan=cropped?Math.max(0,Math.min(100,wallpaperFrame.x-50+wallpaperSectionProgress*100/6)):wallpaperFrame.x;
  const geometry=[baseW,baseH,fit].join('|');
  if(media.wallpaperGeometry!==geometry){media.wallpaperGeometry=geometry;media.style.width=baseW+'px';media.style.height=baseH+'px';media.style.objectFit=fit;media.style.transformOrigin='0 0'}
  const x=(w-baseW*scale)*pan/100,y=(h-baseH*scale)*wallpaperFrame.y/100;
  const transform=`translate3d(${Math.round(x*100)/100}px,${Math.round(y*100)/100}px,0) scale(${scale})`;if(media.style.transform!==transform)media.style.transform=transform;
}
window.setWallpaperSectionProgress=function(progress,dragging=false){
  wallpaperSectionProgress=Math.max(0,Math.min(6,Number(progress)||0));
  const media=document.querySelector('#trialWallpaper video')||document.querySelector('#trialWallpaper img');
  if(media){const transition=dragging?'none':'transform 260ms cubic-bezier(.22,.75,.25,1)';if(media.style.transition!==transition)media.style.transition=transition}
  applyWallpaperFrame();
};
function setWallpaperFrame(part,value){
  if(part==='fit')wallpaperFrame.fit=['height','cover','contain'].includes(value)?value:'height';
  else if(part==='zoom')wallpaperFrame.zoom=Math.max(50,Math.min(250,Number(value)||100));
  else if(part==='x'||part==='y')wallpaperFrame[part]=Math.max(0,Math.min(100,Number(value)||0));
  queueWallpaperAppearance();
}
function saveWallpaperFrame(){localStorage.setItem(wallpaperFrameKey,JSON.stringify(wallpaperFrame));if(wallpaperActiveId){wallpaperFrames[wallpaperActiveId]={...wallpaperFrame};localStorage.setItem(wallpaperFramesKey,JSON.stringify(wallpaperFrames))}}
function resetWallpaperFrame(){wallpaperFrame={fit:'height',zoom:100,x:50,y:50};saveWallpaperFrame();applyWallpaperFrame();syncWallpaperControls()}
function useWallpaperState(state){
  if(!state?.ok)throw Error(state?.error||'读取壁纸失败');
  const nextId=state.activeId||'';
  if(nextId!==wallpaperActiveId){
    if(wallpaperActiveId)wallpaperFrames[wallpaperActiveId]={...wallpaperFrame};
    wallpaperFrame=wallpaperFrames[nextId]?normalizeWallpaperFrame(wallpaperFrames[nextId]):wallpaperActiveId?{fit:'height',zoom:100,x:50,y:50}:{...wallpaperFrame};
    localStorage.setItem(wallpaperFramesKey,JSON.stringify(wallpaperFrames));
    localStorage.setItem(wallpaperFrameKey,JSON.stringify(wallpaperFrame));
  }
  wallpaperMediaUrl=String(state.mediaUrl||'').startsWith('file://')?state.mediaUrl:'';
  wallpaperKind=state.kind||'';wallpaperSource=state.source||'direct';wallpaperRevision=state.revision||0;
  wallpaperActiveId=state.activeId||'';wallpaperGallery=Array.isArray(state.gallery)?state.gallery:[];
}
function wallpaperEscape(value){return String(value??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function wallpaperGalleryUI(){
  if(!wallpaperGallery.length)return '<p class="wallpaper-empty">尚未收藏壁纸；从相册或文件导入后会留在这里。</p>';
  return wallpaperGallery.slice().reverse().map(item=>{
    const id=wallpaperEscape(item.id),label=wallpaperEscape(item.name||'未命名壁纸');
    const cover=item.kind==='image'?`item/${encodeURIComponent(item.id)}`:`thumb/${encodeURIComponent(item.id)}`;
    const icon=`<img loading="lazy" src="https://asphalt.local/wallpaper/${cover}?v=${item.revision||0}${item.kind==='video'&&!wallpaperThumbnailsReady?'&cached=1':''}" alt="" onerror="this.hidden=true">${item.kind==='video'?'<span class="wallpaper-play" aria-hidden="true">▶</span>':''}`;
    const active=wallpaperActiveId===item.id;
    return `<div class="wallpaper-saved${active?' active':''}${wallpaperMenuId===item.id?' menu-open':''}" role="button" tabindex="0" aria-label="${active?'当前壁纸：':'切换到壁纸：'}${label}" onclick="activateSavedWallpaper('${id}')" onkeydown="if(event.target===this&&(event.key==='Enter'||event.key===' ')){event.preventDefault();activateSavedWallpaper('${id}')}"><div class="wallpaper-menu-wrap"><button class="wallpaper-more" type="button" aria-label="${label}的选项" aria-expanded="${wallpaperMenuId===item.id}" onclick="event.stopPropagation();toggleWallpaperMenu('${id}')">⋮</button>${wallpaperMenuId===item.id?`<div class="wallpaper-menu"><button type="button" onclick="event.stopPropagation();renameSavedWallpaper('${id}')">重命名</button><button type="button" class="wallpaper-delete-action" onclick="event.stopPropagation();deleteSavedWallpaper('${id}')">删除</button></div>`:''}</div><div class="wallpaper-preview">${icon}</div><div class="wallpaper-saved-name"><strong title="${label}">${label}</strong><small>${item.kind==='video'?'视频':'图片'} · ${active?'使用中':'点卡片切换'}</small></div></div>`;
  }).join('');
}
function syncWallpaperControls(includeGallery=true){
  const updateText=(node,value)=>{if(node&&node.textContent!==String(value))node.textContent=String(value)};
  const statusText=wallpaperPlaybackError||(wallpaperSource==='mpkg-preview'?'当前：MPKG 场景预览图（静态）':wallpaperSource==='mpkg-video'?'当前：MPKG 内视频':wallpaperKind==='video'?'当前：动态视频':wallpaperKind==='image'?'当前：静态图片':'当前：默认背景');
  for(const suffix of ['','Settings']){
    const find=id=>document.getElementById(id+suffix);
    const wallValue=find('wallpaperTransparencyValue');updateText(wallValue,wallpaperTransparency+'%');
    const wallSlider=find('wallpaperTransparency');if(wallSlider&&Number(wallSlider.value)!==wallpaperTransparency)wallSlider.value=wallpaperTransparency;syncWallpaperSlider(wallSlider);
    const cardValue=find('cardTransparencyValue');updateText(cardValue,cardTransparency+'%');
    const cardSlider=find('cardTransparency');if(cardSlider&&Number(cardSlider.value)!==cardTransparency)cardSlider.value=cardTransparency;syncWallpaperSlider(cardSlider);
    for(const part of ['zoom','x','y']){const id='wallpaperFrame'+part.toUpperCase();const value=find(id+'Value');updateText(value,wallpaperFrame[part]+'%');const slider=find(id);if(slider&&Number(slider.value)!==wallpaperFrame[part])slider.value=wallpaperFrame[part];syncWallpaperSlider(slider)}
    const fit=find('wallpaperFrameFit');if(fit&&fit.value!==wallpaperFrame.fit)fit.value=wallpaperFrame.fit;
    const status=find('wallpaperStatus');updateText(status,statusText);
    const retry=find('wallpaperRetry');if(retry)retry.hidden=wallpaperKind!=='video'||!wallpaperPlaybackError;
    const gallery=find('wallpaperGallery');if(gallery&&includeGallery){const html=wallpaperGalleryUI();if(gallery.wallpaperRenderedHTML!==html){gallery.innerHTML=html;gallery.wallpaperRenderedHTML=html}}
  }
}
function chooseWallpaper(kind){if(!window.Android?.pickWallpaper)return alert('请在安卓测试版中选择壁纸');Android.pickWallpaper(kind)}
function clearTrialWallpaper(){if(!window.Android?.clearWallpaper)return;try{useWallpaperState(JSON.parse(Android.clearWallpaper()));applyWallpaper()}catch(e){alert('恢复默认背景失败：'+e.message)}}
function toggleWallpaperMenu(id){wallpaperMenuId=wallpaperMenuId===id?null:id;syncWallpaperControls()}
function activateSavedWallpaper(id){wallpaperMenuId=null;if(id===wallpaperActiveId){syncWallpaperControls();return}if(!window.Android?.activateWallpaper)return;try{useWallpaperState(JSON.parse(Android.activateWallpaper(id)));applyWallpaper()}catch(e){alert('切换壁纸失败：'+e.message)}}
function renameSavedWallpaper(id){
  if(!window.Android?.renameWallpaper)return;
  const item=wallpaperGallery.find(entry=>entry.id===id);if(!item)return;
  const entered=prompt('壁纸新名称',item.name||'未命名壁纸');if(entered===null)return;
  const name=entered.trim();
  if(!name||name.length>80||/[\x00-\x1f\x7f]/.test(name)){alert('名称请填写 1 至 80 个字符，且不要换行');return}
  try{useWallpaperState(JSON.parse(Android.renameWallpaper(id,name)));wallpaperMenuId=null;syncWallpaperControls()}
  catch(e){alert('重命名失败：'+e.message)}
}
function deleteSavedWallpaper(id){if(!window.Android?.deleteWallpaper)return;if(!confirm('删除这张已保存的壁纸吗？'))return;try{useWallpaperState(JSON.parse(Android.deleteWallpaper(id)));wallpaperMenuId=null;applyWallpaper()}catch(e){alert('删除壁纸失败：'+e.message)}}
window.onWallpaperEvent=function(event){if(event.kind==='error'){alert('设置壁纸失败：'+event.message);return}if(event.kind!=='saved')return;try{useWallpaperState(JSON.parse(Android.wallpaperState()));applyWallpaper()}catch(e){alert('读取壁纸失败：'+e.message)}};
function drawerWorkbenchUI(){
  const tasks=taskBuckets(),due=tasks.action.slice(0,2),running=tasks.timing.slice(0,2);
  const recent=[...db.samples].sort((a,b)=>(b.date||localDateKey(b.created)).localeCompare(a.date||localDateKey(a.created))||b.created-a.created).slice(0,3);
  return `<section class="drawer-workbench"><div class="drawer-work-head"><strong>工作速览</strong><span>${tasks.action.length?tasks.action.length+' 项待处理':'实验进行中'}</span></div>${due.length?`<p class="drawer-work-label">到点待处理</p>${due.map(t=>`<button class="drawer-task is-due" onclick="openTask('${t.run.id}')"><span><b>${esc(t.sample.name)}</b><small>${esc(t.run.name)} · ${esc(t.step.name)}</small></span><i>查看 ›</i></button>`).join('')}`:''}${running.length?`<p class="drawer-work-label">即将到点</p>${running.map(t=>`<button class="drawer-task" onclick="openTask('${t.run.id}')"><span><b>${esc(t.sample.name)}</b><small>${esc(t.run.name)}</small></span><i>${esc(taskDuration(t.due-Date.now()))}</i></button>`).join('')}`:''}${!due.length&&!running.length?'<p class="drawer-work-empty">暂无到点或计时中的实验</p>':''}<p class="drawer-work-label">最近样品</p>${recent.map(s=>`<button class="drawer-task" onclick="routeTo('samples','${s.date||localDateKey(s.created)}','${s.id}')"><span><b>${esc(s.name)}</b><small>${esc(s.date||localDateKey(s.created))}</small></span><i>›</i></button>`).join('')||'<p class="drawer-work-empty">暂无样品记录</p>'}</section>`;
}
function drawerWallpaperUI(scope='drawer'){let html=`<details class="drawer-appearance"><summary>壁纸设置</summary><p id="wallpaperStatus">当前：默认背景</p><div class="drawer-wall-actions"><button onclick="chooseWallpaper('image')">从相册选图片</button><button onclick="chooseWallpaper('video')">从相册选视频</button></div><div class="drawer-wall-actions"><button onclick="chooseWallpaper('mpkg')">从文件导入 .mpkg</button></div><div class="wallpaper-gallery"><strong>已导入的壁纸</strong><div id="wallpaperGallery">${wallpaperGalleryUI()}</div></div><div class="wallpaper-frame-controls"><label for="wallpaperFrameFit">画面显示方式</label><select id="wallpaperFrameFit" onchange="setWallpaperFrame('fit',this.value)"><option value="height">优先匹配屏幕高度（推荐）</option><option value="cover">铺满屏幕（允许裁切）</option><option value="contain">完整显示（可能留边）</option></select><label for="wallpaperFrameZOOM">画面大小 <span id="wallpaperFrameZOOMValue">${wallpaperFrame.zoom}%</span></label>${wallpaperSliderUI('wallpaperFrameZOOM',50,250,5,wallpaperFrame.zoom,"setWallpaperFrame('zoom',this.value)",'画面大小')}<label for="wallpaperFrameX">左右位置 <span id="wallpaperFrameXValue">${wallpaperFrame.x}%</span></label>${wallpaperSliderUI('wallpaperFrameX',0,100,1,wallpaperFrame.x,"setWallpaperFrame('x',this.value)",'左右位置')}<small>宽幅壁纸会随功能区横向移动；这里可微调画面位置。</small><label for="wallpaperFrameY">上下位置 <span id="wallpaperFrameYValue">${wallpaperFrame.y}%</span></label>${wallpaperSliderUI('wallpaperFrameY',0,100,1,wallpaperFrame.y,"setWallpaperFrame('y',this.value)",'上下位置')}<button class="secondary" onclick="resetWallpaperFrame()">按屏幕高度适配</button></div><label for="wallpaperTransparency">壁纸淡化 <span id="wallpaperTransparencyValue">${wallpaperTransparency}%</span></label>${wallpaperSliderUI('wallpaperTransparency',0,95,1,wallpaperTransparency,"setWallpaperTransparency(this.value)",'壁纸淡化')}<small>0% 显示原色；数值越大，壁纸越淡。</small><label for="cardTransparency">玻璃卡片透明度 <span id="cardTransparencyValue">${cardTransparency}%</span></label>${wallpaperSliderUI('cardTransparency',45,100,1,cardTransparency,"setCardTransparency(this.value)",'玻璃卡片透明度')}<div class="drawer-wall-actions"><button onclick="setWallpaperTransparency(0)">壁纸原色</button><button id="wallpaperRetry" onclick="retryWallpaperVideo()" hidden>重新播放</button><button class="secondary" onclick="clearTrialWallpaper()">恢复默认背景</button></div><small>恢复默认背景会保留已导入列表。支持视频和 GIF 动图；MPKG 视频可播放，场景包仅显示预览图（原场景特效、时钟、音频组件不会运行）。退出软件时视频暂停。</small></details>`;
  if(scope==='settings')html=html.replace('<details class="drawer-appearance">','<details class="drawer-appearance wallpaper-settings" open>').replace(/\b(id|for|data-input)="([^"]+)"/g,(_,attr,id)=>attr+'="'+id+'Settings"');
  return html;
}
function refreshDrawerWorkbench(){const top=document.getElementById('drawerWallpaperTop'),el=document.getElementById('drawerWorkbench');if(top)top.innerHTML=drawerWallpaperUI();if(el)el.innerHTML=drawerWorkbenchUI();syncWallpaperControls()}


let wallpaperSliderDrag=null;
let wallpaperAppearanceFrame=0,cardAppearanceFrame=0,wallpaperPreferencesTimer=0,wallpaperViewport=null,cardPaintedOpacity=null;
function wallpaperSliderUI(id,min,max,step,value,action,label){
 const percent=Math.max(0,Math.min(100,(Number(value)-min)/(max-min)*100));
 return `<div class="wallpaper-slider"><input id="${id}" type="hidden" min="${min}" max="${max}" step="${step}" value="${value}" oninput="${action}"><div class="wallpaper-slider-track"><span class="wallpaper-slider-knob" role="slider" tabindex="0" data-input="${id}" aria-label="${wallpaperEscape(label)}" aria-valuemin="${min}" aria-valuemax="${max}" aria-valuenow="${value}" aria-valuetext="${value}%" style="left:${percent}%"></span></div></div>`;
}
function syncWallpaperSlider(input){
 const knob=input?.parentElement?.querySelector('.wallpaper-slider-knob');if(!knob)return;
 const min=Number(input.min),max=Number(input.max),value=Number(input.value);
 const left=Math.max(0,Math.min(100,(value-min)/(max-min)*100))+'%';if(knob.style.left!==left)knob.style.left=left;
 if(knob.getAttribute('aria-valuenow')!==String(value)){knob.setAttribute('aria-valuenow',String(value));knob.setAttribute('aria-valuetext',value+'%')}
}
function wallpaperSliderTarget(target){
 const knob=target?.closest?.('.wallpaper-slider-knob'),wrapper=knob?.closest('.wallpaper-slider'),input=wrapper?.querySelector('input');
 return input&&!input.disabled?{knob,wrapper,input}:null;
}
function initWallpaperSliders(){
 const point=e=>e.touches?.[0]||e.changedTouches?.[0]||e;
 const pointerId=e=>e.pointerId??point(e).identifier??'mouse';
 const setValue=(input,value)=>{const min=Number(input.min),max=Number(input.max),step=Number(input.step||1);value=Math.max(min,Math.min(max,min+Math.round((value-min)/step)*step));if(Number(input.value)!==value){input.value=String(Math.round(value*1e8)/1e8);input.dispatchEvent(new Event('input',{bubbles:true}));syncWallpaperSlider(input)}};
 const down=e=>{
  const target=wallpaperSliderTarget(e.target);
  if(!target){if(e.target?.closest?.('.wallpaper-slider'))e.stopPropagation();return}
  if(e.button!==undefined&&e.button!==0)return;
  if(wallpaperSliderDrag)return;
  const {input,knob,wrapper}=target,p=point(e),rect=wrapper.querySelector('.wallpaper-slider-track').getBoundingClientRect(),thumb=knob.getBoundingClientRect();
  e.preventDefault();e.stopPropagation();
  knob.focus({preventScroll:true});
  wallpaperSliderDrag={input,knob,id:pointerId(e),rect,min:Number(input.min),max:Number(input.max),grab:p.clientX-(thumb.left+thumb.width/2),start:input.value};
  if(e.pointerId!==undefined)try{knob.setPointerCapture(e.pointerId)}catch(_){}
 };
 const move=e=>{
  const drag=wallpaperSliderDrag;if(!drag||pointerId(e)!==drag.id)return;
  if(!drag.input.isConnected){wallpaperSliderDrag=null;return}
  e.preventDefault();e.stopPropagation();const p=point(e);if(drag.rect.width<=0)return;
  setValue(drag.input,drag.min+(p.clientX-drag.grab-drag.rect.left)/drag.rect.width*(drag.max-drag.min));
 };
 const up=e=>{
  const drag=wallpaperSliderDrag;if(!drag||pointerId(e)!==drag.id)return;
  move(e);wallpaperSliderDrag=null;flushWallpaperPreferences();
  if(drag.input.value!==drag.start)drag.input.dispatchEvent(new Event('change',{bubbles:true}));
  if(e.pointerId!==undefined)try{drag.knob.releasePointerCapture(e.pointerId)}catch(_){}
 };
 const options={capture:true,passive:false};
 if(window.PointerEvent){
  document.addEventListener('pointerdown',down,options);document.addEventListener('pointermove',move,options);document.addEventListener('pointerup',up,options);document.addEventListener('pointercancel',()=>{wallpaperSliderDrag=null},options);
  // Android may deliver touch events alongside Pointer Events. Keep them away
  // from page/drawer swipe handlers without relying on native range defaults.
  for(const name of ['touchstart','touchmove','touchend'])document.addEventListener(name,e=>{if(e.target?.closest?.('.wallpaper-slider')){e.stopPropagation();if(wallpaperSliderTarget(e.target))e.preventDefault()}},options);
 }
 else{document.addEventListener('touchstart',down,options);document.addEventListener('touchmove',move,options);document.addEventListener('touchend',up,options);document.addEventListener('touchcancel',()=>{wallpaperSliderDrag=null},options);document.addEventListener('mousedown',down,options);document.addEventListener('mousemove',move,options);document.addEventListener('mouseup',up,options)}
 document.addEventListener('click',e=>{if(e.target?.closest?.('.wallpaper-slider')){e.preventDefault();e.stopPropagation()}},options);
 document.addEventListener('keydown',e=>{const target=wallpaperSliderTarget(e.target);if(!target)return;const {input}=target,step=Number(input.step||1);let value=Number(input.value);if(['ArrowRight','ArrowUp'].includes(e.key))value+=step;else if(['ArrowLeft','ArrowDown'].includes(e.key))value-=step;else if(e.key==='Home')value=Number(input.min);else if(e.key==='End')value=Number(input.max);else return;e.preventDefault();e.stopPropagation();setValue(input,value)},options);
 document.addEventListener('visibilitychange',()=>{if(document.hidden)wallpaperSliderDrag=null},options);
}
