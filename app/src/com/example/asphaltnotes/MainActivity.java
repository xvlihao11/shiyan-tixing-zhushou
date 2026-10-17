package com.example.asphaltnotes.cloudtrial;

import android.Manifest;
import android.app.Activity;
import android.app.AlarmManager;
import android.app.PendingIntent;
import android.app.Notification;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebResourceRequest;
import android.webkit.ValueCallback;
import android.net.Uri;
import android.content.ClipData;
import android.content.ContentValues;
import android.provider.MediaStore;
import android.os.Environment;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.KeyEvent;
import android.widget.Toast;
import org.json.JSONObject;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
  private static volatile MainActivity activeBackupActivity;
  private static volatile boolean backupNativeWorking=false,textExportNativeWorking=false;
  private static final ExecutorService fileExecutor=Executors.newSingleThreadExecutor();
  private WebView web;
  private ValueCallback<Uri[]> fileCallback;
  private Uri ocrPhoto;
  private String ocrRunId, ocrGroupId, ocrRunName;
  private boolean ocrFromCamera, ocrAutoRecognize;
  private boolean webReady=false;
  private boolean activityResumed=false;
  private int wallpaperResumeToken=0;
  private String wallpaperPickKind="";
  private final ExecutorService cloudExecutor=Executors.newSingleThreadExecutor();
  static final int CREATE_DOC=40, PICK_FILE=41, PHOTO_CAPTURE=42, PHOTO_PICK=43, PHOTO_STORAGE_PERMISSION=44, BACKUP_SAVE=45, BACKUP_OPEN=46, WALLPAPER_PICK=47;

  @Override public void onCreate(Bundle b) {
    super.onCreate(b);activeBackupActivity=this;if(new TextExport(this).receiving())new TextExport(this).clear();if(new TextExport(this).writing()&&!textExportNativeWorking){new TextExport(this).clear();textExportResult("上次导出被中断，请重新导出备份",false);}
    if(b!=null){String saved=b.getString("ocrPhoto");if(saved!=null)ocrPhoto=Uri.parse(saved);ocrRunId=b.getString("ocrRunId");ocrGroupId=b.getString("ocrGroupId");ocrRunName=b.getString("ocrRunName");ocrFromCamera=b.getBoolean("ocrFromCamera");ocrAutoRecognize=b.getBoolean("ocrAutoRecognize");}
    if(backupState().equals("export-staging")||(!backupNativeWorking&&(backupState().equals("export-writing")||backupState().equals("restore-loading")))){endBackup("error","备份传输被中断，请重新导出");}
    ensureNotificationChannels();
    requestNotificationPermissionIfNeeded();
    web=new WebView(this);web.setBackgroundColor(0xff153d59);preferSmoothRefresh();web.getSettings().setJavaScriptEnabled(true);web.getSettings().setDomStorageEnabled(true);
    web.getSettings().setAllowFileAccess(true);web.getSettings().setMediaPlaybackRequiresUserGesture(false);
    web.addJavascriptInterface(new Bridge(),"Android");web.setWebViewClient(new WebViewClient(){
      @Override public void onPageFinished(WebView view,String url){webReady=true;view.evaluateJavascript("if(window.setWallpaperAppBackground)setWallpaperAppBackground("+(!activityResumed)+")",null);deliverPhotoEvents();deliverBackupEvents();}
      @Override public android.webkit.WebResourceResponse shouldInterceptRequest(WebView view,WebResourceRequest request){android.webkit.WebResourceResponse wall=WallpaperStore.response(MainActivity.this,request.getUrl(),request.getRequestHeaders());return wall!=null?wall:PhotoStore.response(MainActivity.this,request.getUrl());}
      @Override public boolean shouldOverrideUrlLoading(WebView view,WebResourceRequest request){return !"file".equals(request.getUrl().getScheme())||!request.getUrl().toString().startsWith("file:///android_asset/");}
      @Override public boolean shouldOverrideUrlLoading(WebView view,String url){return !url.startsWith("file:///android_asset/");}
    });
    web.setWebChromeClient(new WebChromeClient(){
      @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams params){
        if(fileCallback!=null)fileCallback.onReceiveValue(null);
        fileCallback=cb;Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);
        String[] accepts=params.getAcceptTypes();boolean json=false;
        if(accepts!=null)for(String a:accepts)if(a.contains("json"))json=true;
        intent.setType(json?"application/json":"*/*");
        if(!json)intent.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/pdf","text/markdown","text/plain","application/octet-stream"});
        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE,params.getMode()==FileChooserParams.MODE_OPEN_MULTIPLE);
        try{startActivityForResult(intent,PICK_FILE);}catch(Exception e){fileCallback.onReceiveValue(null);fileCallback=null;Toast.makeText(MainActivity.this,"无法打开文件选择器",Toast.LENGTH_LONG).show();}return true;
      }
    });
    setContentView(web);web.post(this::preferSmoothRefresh);web.loadUrl("file:///android_asset/index.html");
    cloudExecutor.execute(()->migrateNextPhoto(PhotoStore.legacyIds(getApplicationContext()).iterator()));
  }
  private void preferSmoothRefresh(){
    android.view.Display display=getWindowManager().getDefaultDisplay();
    android.view.Display.Mode active=display.getMode();float preferred=active.getRefreshRate();
    for(android.view.Display.Mode mode:display.getSupportedModes())
      if(mode.getPhysicalWidth()==active.getPhysicalWidth()&&mode.getPhysicalHeight()==active.getPhysicalHeight())preferred=Math.max(preferred,mode.getRefreshRate());
    if(preferred>60.1f){android.view.WindowManager.LayoutParams params=getWindow().getAttributes();params.preferredRefreshRate=preferred;getWindow().setAttributes(params);}
    if(Build.VERSION.SDK_INT>=35)web.setRequestedFrameRate(android.view.View.REQUESTED_FRAME_RATE_CATEGORY_HIGH);
  }
  private void migrateNextPhoto(java.util.Iterator<String> ids){
    if(cloudExecutor.isShutdown()||!ids.hasNext())return;
    try{PhotoStore.ensureCompact(getApplicationContext(),ids.next());}catch(Exception ignored){}
    // Queue one conversion at a time so new attachments and OCR can run between them.
    if(!cloudExecutor.isShutdown()&&ids.hasNext()){
      try{cloudExecutor.execute(()->migrateNextPhoto(ids));}catch(java.util.concurrent.RejectedExecutionException ignored){}
    }
  }
  private void ensureNotificationChannels(){
    if(Build.VERSION.SDK_INT>=26){
      android.app.NotificationManager nm=(android.app.NotificationManager)getSystemService(NOTIFICATION_SERVICE);
      // Android keeps notification channels after app updates. Remove duplicates created by earlier builds.
      nm.deleteNotificationChannel("notice_v1");
      nm.deleteNotificationChannel("ring_v1");
      nm.deleteNotificationChannel("notice_v2");
      nm.deleteNotificationChannel("ring_v2");
      android.app.NotificationChannel notice=new android.app.NotificationChannel("notice_v3","实验通知提醒",android.app.NotificationManager.IMPORTANCE_HIGH);
      notice.enableVibration(true);notice.setLockscreenVisibility(android.app.Notification.VISIBILITY_PUBLIC);nm.createNotificationChannel(notice);
      android.app.NotificationChannel ring=new android.app.NotificationChannel("ring_v3","实验响铃提醒",android.app.NotificationManager.IMPORTANCE_HIGH);
      ring.enableVibration(true);ring.setLockscreenVisibility(android.app.Notification.VISIBILITY_PUBLIC);ring.setSound(android.provider.Settings.System.DEFAULT_ALARM_ALERT_URI,new android.media.AudioAttributes.Builder().setUsage(android.media.AudioAttributes.USAGE_ALARM).build());nm.createNotificationChannel(ring);
    }
  }
  private void requestNotificationPermissionIfNeeded(){
    if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)
      requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},1);
  }
  public class Bridge {
    @JavascriptInterface public void cancel(String id){AlarmScheduler.cancel(MainActivity.this,id);}
    @JavascriptInterface public String reminderDiagnostics(){return AlarmScheduler.diagnostics(MainActivity.this);}
    @JavascriptInterface public boolean notificationsEnabled(){
      if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=getPackageManager().PERMISSION_GRANTED)return false;
      android.app.NotificationManager nm=(android.app.NotificationManager)getSystemService(NOTIFICATION_SERVICE);
      if(!nm.areNotificationsEnabled())return false;
      if(Build.VERSION.SDK_INT>=26){
        android.app.NotificationChannel notice=nm.getNotificationChannel("notice_v3"),ring=nm.getNotificationChannel("ring_v3");
        if(notice!=null&&notice.getImportance()==android.app.NotificationManager.IMPORTANCE_NONE)return false;
        if(ring!=null&&ring.getImportance()==android.app.NotificationManager.IMPORTANCE_NONE)return false;
      }
      return true;
    }
    @JavascriptInterface public void requestNotifications(){runOnUiThread(()->{
      if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=getPackageManager().PERMISSION_GRANTED)requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS},1);
      else Toast.makeText(MainActivity.this,"请在系统设置中允许此应用发送通知",Toast.LENGTH_LONG).show();
    });}
    @JavascriptInterface public boolean exactAlarmEnabled(){return Build.VERSION.SDK_INT<31||((AlarmManager)getSystemService(ALARM_SERVICE)).canScheduleExactAlarms();}
    @JavascriptInterface public void requestExactAlarm(){runOnUiThread(()->{
      if(Build.VERSION.SDK_INT>=31&&!((AlarmManager)getSystemService(ALARM_SERVICE)).canScheduleExactAlarms())startActivity(new Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM,Uri.parse("package:"+getPackageName())));
      else Toast.makeText(MainActivity.this,"精确闹钟权限已开启",Toast.LENGTH_SHORT).show();
    });}
    @JavascriptInterface public void openNotificationSettings(){runOnUiThread(()->{
      Intent i=new Intent("android.settings.APP_NOTIFICATION_SETTINGS");i.putExtra("android.provider.extra.APP_PACKAGE",getPackageName());startActivity(i);
    });}
    @JavascriptInterface public void openAppSettings(){runOnUiThread(()->{
      Intent i=new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,Uri.parse("package:"+getPackageName()));startActivity(i);
    });}
    @JavascriptInterface public void showTestNotification(boolean ring){
      if(Build.VERSION.SDK_INT>=33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED){
        runOnUiThread(()->Toast.makeText(MainActivity.this,"请先在“检查并设置提醒”中开启通知",Toast.LENGTH_LONG).show());return;
      }
      String channel=ring?"ring_v3":"notice_v3";
      Notification.Builder builder=Build.VERSION.SDK_INT>=26?new Notification.Builder(MainActivity.this,channel):new Notification.Builder(MainActivity.this);
      Notification n=builder.setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
        .setContentTitle(ring?"闹铃测试":"通知测试")
        .setContentText(ring?"测试闹铃已触发":"测试通知已发送")
        .setVisibility(Notification.VISIBILITY_PUBLIC)
        .setCategory(ring?Notification.CATEGORY_ALARM:Notification.CATEGORY_REMINDER)
        .setPriority(Notification.PRIORITY_MAX)
        .setAutoCancel(true)
        .build();
      ((android.app.NotificationManager)getSystemService(NOTIFICATION_SERVICE)).notify(ring?30502:30501,n);
    }
    @JavascriptInterface public boolean schedule(String id,long at,String title,String message,boolean ring){
      return AlarmScheduler.schedule(MainActivity.this,id,at,title,message,ring);
    }
    @JavascriptInterface public boolean isScheduled(String id,long at,boolean ring){
      return AlarmScheduler.isScheduled(MainActivity.this,id,at,ring);
    }
    @JavascriptInterface public String saveFile(String name,String content,String type){return backupCall(()->{if(!backupState().isEmpty())throw new Exception("备份正在进行");new TextExport(MainActivity.this).prepare(name,content,type);runOnUiThread(()->openTextExportPicker());return new JSONObject();});}
    @JavascriptInterface public String textExportStart(String name,String type){return backupCall(()->{if(!backupState().isEmpty())throw new Exception("备份正在进行");new TextExport(MainActivity.this).start(name,type);return new JSONObject();});}
    @JavascriptInterface public String textExportAppend(String data){return backupCall(()->{new TextExport(MainActivity.this).append(data);return new JSONObject();});}
    @JavascriptInterface public String textExportFinish(){return backupCall(()->{new TextExport(MainActivity.this).finish();runOnUiThread(()->openTextExportPicker());return new JSONObject();});}
    @JavascriptInterface public String textExportFinishLocal(){if(Build.VERSION.SDK_INT<29)return textExportFinish();return backupCall(()->{new TextExport(MainActivity.this).finish();startTextExport(null,true);return new JSONObject();});}
    @JavascriptInterface public void textExportDiscard(){new TextExport(MainActivity.this).clear();}
    private String backupCall(BackupAction action){try{JSONObject r=action.run();return r.put("ok",true).toString();}catch(Exception e){try{return new JSONObject().put("ok",false).put("error",String.valueOf(e.getMessage())).toString();}catch(Exception ignored){return "{\"ok\":false}";}}}
    @JavascriptInterface public String backupStart(){return backupCall(()->{String state=backupState();if(!state.isEmpty()||new TextExport(MainActivity.this).busy())throw new Exception("已有备份或导出正在进行");new BackupArchive(MainActivity.this).begin();setBackupState("export-staging");return new JSONObject();});}
    @JavascriptInterface public String backupAppend(String name,String chunk,boolean first){return backupCall(()->{if(!backupState().equals("export-staging"))throw new Exception("备份状态无效");new BackupArchive(MainActivity.this).append(name,chunk,first);return new JSONObject();});}
    @JavascriptInterface public String backupFinish(String photoIds,boolean photos){return backupCall(()->{if(!backupState().equals("export-staging"))throw new Exception("备份状态无效");new BackupArchive(MainActivity.this).prepare(photoIds,photos);setBackupState("export-picking");runOnUiThread(()->{try{Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.setType("application/zip");i.addCategory(Intent.CATEGORY_OPENABLE);i.putExtra(Intent.EXTRA_TITLE,"沥青实验完整备份_"+System.currentTimeMillis()+".zip");startActivityForResult(i,BACKUP_SAVE);}catch(Exception e){endBackup("error",e.getMessage());}});return new JSONObject();});}
    @JavascriptInterface public String backupFinishLocal(String photoIds,boolean photos){
      if(Build.VERSION.SDK_INT<29)return backupFinish(photoIds,photos);
      return backupCall(()->{if(!backupState().equals("export-staging"))throw new Exception("备份状态无效");new BackupArchive(MainActivity.this).prepare(photoIds,photos);backupNativeWorking=true;setBackupState("export-writing");fileExecutor.execute(()->{
        Uri target=null;try{target=LocalExport.create(MainActivity.this,"沥青实验完整备份_"+System.currentTimeMillis()+".zip","application/zip");long size=new BackupArchive(MainActivity.this).export(target);LocalExport.publish(MainActivity.this,target);setBackupState("");queueBackupEvent("saved",new JSONObject().put("message","ZIP 备份已保存并校验（"+size+" 字节）\n位置：下载/沥青实验记录"));}catch(Exception e){LocalExport.discard(MainActivity.this,target);endBackup("error",e.getMessage());}finally{backupNativeWorking=false;}
      });return new JSONObject();});
    }
    @JavascriptInterface public String backupOpen(){return backupCall(()->{if(!backupState().isEmpty()||new TextExport(MainActivity.this).busy())throw new Exception("已有备份或导出正在进行");setBackupState("restore-picking");runOnUiThread(()->{try{Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT);i.setType("*/*");i.addCategory(Intent.CATEGORY_OPENABLE);i.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,android.provider.DocumentsContract.buildDocumentUri("com.android.externalstorage.documents","primary:Download/沥青实验记录"));startActivityForResult(i,BACKUP_OPEN);}catch(Exception e){endBackup("error",e.getMessage());}});return new JSONObject();});}
    @JavascriptInterface public String backupReadInfo(String name){return backupCall(()->{if(!backupState().equals("restore-ready"))throw new Exception("恢复状态无效");return new BackupArchive(MainActivity.this).info(name);});}
    @JavascriptInterface public String backupReadChunk(String name,long offset){return backupCall(()->{if(!backupState().equals("restore-ready"))throw new Exception("恢复状态无效");return new JSONObject().put("data",new BackupArchive(MainActivity.this).chunk(name,offset));});}
    @JavascriptInterface public String backupCommitPhotos(){return backupCall(()->{if(!backupState().equals("restore-ready"))throw new Exception("恢复状态无效");new BackupArchive(MainActivity.this).commitPhotos();return new JSONObject();});}
    @JavascriptInterface public String backupRollbackPhotos(){return backupCall(()->{new BackupArchive(MainActivity.this).rollbackPhotos();return new JSONObject();});}
    @JavascriptInterface public void backupDiscard(){new BackupArchive(MainActivity.this).clear();setBackupState("");getSharedPreferences("backup_events",MODE_PRIVATE).edit().clear().commit();}
    @JavascriptInterface public void acknowledgeBackupEvent(String id){getSharedPreferences("backup_events",MODE_PRIVATE).edit().remove(id).commit();}
    @JavascriptInterface public boolean hasArkKey(){return ApiKeyStore.read(MainActivity.this)!=null;}
    @JavascriptInterface public boolean saveArkKey(String key){return ApiKeyStore.save(MainActivity.this,key);}
    @JavascriptInterface public void clearArkKey(){ApiKeyStore.clear(MainActivity.this);}
    @JavascriptInterface public void captureForGroup(String runId,String groupId,String runName){
      runOnUiThread(()->{ocrAutoRecognize=true;startOcrCamera(runId,groupId,runName);});
    }
    @JavascriptInterface public void pickPhotoForGroup(String runId,String groupId,String runName){
      runOnUiThread(()->{ocrAutoRecognize=true;startOcrPicker(runId,groupId,runName);});
    }
    @JavascriptInterface public void attachPhotoForGroup(String runId,String groupId,String name,boolean camera,boolean recognize){
      runOnUiThread(()->{ocrAutoRecognize=recognize;if(camera)startOcrCamera(runId,groupId,name);else startOcrPicker(runId,groupId,name);});
    }
    @JavascriptInterface public String rotateExperimentPhoto(String id){return backupCall(()->{PhotoStore.rotate(MainActivity.this,id);return new JSONObject();});}
    @JavascriptInterface public String deleteExperimentPhoto(String id){return backupCall(()->{PhotoStore.delete(MainActivity.this,id);return new JSONObject();});}
    @JavascriptInterface public String wallpaperState(){return backupCall(()->WallpaperStore.state(MainActivity.this));}
    @JavascriptInterface public String clearWallpaper(){return backupCall(()->WallpaperStore.clear(MainActivity.this));}
    @JavascriptInterface public String activateWallpaper(String id){return backupCall(()->WallpaperStore.activate(MainActivity.this,id));}
    @JavascriptInterface public String renameWallpaper(String id,String name){return backupCall(()->WallpaperStore.rename(MainActivity.this,id,name));}
    @JavascriptInterface public String deleteWallpaper(String id){return backupCall(()->WallpaperStore.remove(MainActivity.this,id));}
    @JavascriptInterface public void pickWallpaper(String kind){
      if(!kind.equals("image")&&!kind.equals("video")&&!kind.equals("mpkg"))return;
      runOnUiThread(()->{try{wallpaperPickKind=kind;String mime=kind.equals("mpkg")?"*/*":kind.equals("video")?"video/*":"image/*";
        Intent intent=kind.equals("mpkg")?new Intent(Intent.ACTION_OPEN_DOCUMENT):Build.VERSION.SDK_INT>=33?new Intent(MediaStore.ACTION_PICK_IMAGES):new Intent(Intent.ACTION_PICK,kind.equals("video")?MediaStore.Video.Media.EXTERNAL_CONTENT_URI:MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
        if(kind.equals("mpkg"))intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mime);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        if(intent.resolveActivity(getPackageManager())==null){intent=new Intent(Intent.ACTION_GET_CONTENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType(mime);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);}
        startActivityForResult(intent,WALLPAPER_PICK);
      }catch(Exception e){notifyWallpaper("error",e.getMessage());}});
    }
    @JavascriptInterface public void acknowledgePhotoEvent(String id){getSharedPreferences("photo_events",MODE_PRIVATE).edit().remove(id).commit();}
    @JavascriptInterface public void recognizeAttachment(String runId,String groupId,String name,String photoId,String attemptId){
      cloudExecutor.execute(()->{
        JSONObject result;
        try{java.io.File f=PhotoStore.ensureCompact(MainActivity.this,photoId);
          if(!f.isFile())throw new Exception("照片附件不在本机，请重新添加照片");
          result=CloudRecognizer.recognize(MainActivity.this,Uri.fromFile(f),name,ApiKeyStore.read(MainActivity.this));
        }catch(Exception e){result=new JSONObject();try{result.put("error",e.getMessage());}catch(Exception ignored){}}
        try{result.put("photoId",photoId).put("attemptId",attemptId);}catch(Exception ignored){}
        queuePhotoEvent("result",runId,groupId,result);
      });
    }
  }
  private interface BackupAction { JSONObject run() throws Exception; }
  private String backupState(){return getSharedPreferences("backup_state",MODE_PRIVATE).getString("operation","");}
  private void setBackupState(String state){getSharedPreferences("backup_state",MODE_PRIVATE).edit().putString("operation",state).commit();}
  private void queueBackupEvent(String type,JSONObject payload){try{String id=java.util.UUID.randomUUID().toString();JSONObject event=new JSONObject().put("eventId",id).put("type",type).put("payload",payload);getSharedPreferences("backup_events",MODE_PRIVATE).edit().putString(id,event.toString()).commit();runOnUiThread(()->{MainActivity current=activeBackupActivity;if(current!=null)current.deliverBackupEvents();});}catch(Exception ignored){}}
  private void endBackup(String type,String message){new BackupArchive(this).clear();setBackupState("");try{queueBackupEvent(type,new JSONObject().put("message",message));}catch(Exception ignored){}}
  private void deliverBackupEvents(){if(!webReady||web==null)return;for(Object value:getSharedPreferences("backup_events",MODE_PRIVATE).getAll().values()){try{web.evaluateJavascript("if(window.onNativeBackupEvent)onNativeBackupEvent("+new JSONObject((String)value).toString()+")",null);}catch(Exception ignored){}}}
  private void startOcrCamera(String runId,String groupId,String runName){
    ocrRunId=runId;ocrGroupId=groupId;ocrRunName=runName;ocrFromCamera=true;
    if(Build.VERSION.SDK_INT<=28&&checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)!=PackageManager.PERMISSION_GRANTED){
      requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},PHOTO_STORAGE_PERMISSION);return;
    }
    try{
      ContentValues values=new ContentValues();
      values.put(MediaStore.Images.Media.DISPLAY_NAME,"沥青实验_"+System.currentTimeMillis()+".jpg");
      values.put(MediaStore.Images.Media.MIME_TYPE,"image/jpeg");
      if(Build.VERSION.SDK_INT>=29)values.put(MediaStore.Images.Media.RELATIVE_PATH,Environment.DIRECTORY_PICTURES+"/沥青实验记录");
      ocrPhoto=getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI,values);
      if(ocrPhoto==null)throw new Exception("无法在相册创建照片");
      Intent camera=new Intent(MediaStore.ACTION_IMAGE_CAPTURE);
      camera.putExtra(MediaStore.EXTRA_OUTPUT,ocrPhoto);
      camera.setClipData(ClipData.newUri(getContentResolver(),"实验读数",ocrPhoto));
      camera.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
      startActivityForResult(camera,PHOTO_CAPTURE);
    }catch(Exception e){discardPendingCameraPhoto();deliverOcrError("无法打开相机："+e.getMessage());}
  }
  private void startOcrPicker(String runId,String groupId,String runName){
    try{
      ocrRunId=runId;ocrGroupId=groupId;ocrRunName=runName;ocrFromCamera=false;ocrPhoto=null;
      // Photo-specific picker on Android 13+, gallery ACTION_PICK on older systems.
      Intent pick;
      if(Build.VERSION.SDK_INT>=33){
        pick=new Intent("android.provider.action.PICK_IMAGES");pick.setType("image/*");
        try{startActivityForResult(pick,PHOTO_PICK);return;}catch(android.content.ActivityNotFoundException ignored){}
      }
      pick=new Intent(Intent.ACTION_PICK,MediaStore.Images.Media.EXTERNAL_CONTENT_URI);
      pick.setType("image/*");pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
      startActivityForResult(pick,PHOTO_PICK);
    }catch(Exception e){deliverOcrError("无法选择照片："+e.getMessage());}
  }
  private void discardPendingCameraPhoto(){if(ocrPhoto!=null&&ocrFromCamera){try{getContentResolver().delete(ocrPhoto,null,null);}catch(Exception ignored){}ocrPhoto=null;}}
  private boolean photoHasPixels(Uri uri){
    if(uri==null)return false;
    try(InputStream in=getContentResolver().openInputStream(uri)){
      BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;
      BitmapFactory.decodeStream(in,null,options);
      return options.outWidth>0&&options.outHeight>0;
    }catch(Exception ignored){return false;}
  }
  private boolean recoverCameraPhoto(Intent data){
    if(photoHasPixels(ocrPhoto))return true;
    if(data==null||ocrPhoto==null)return false;
    try{
      if(data.getData()!=null){
        try(InputStream in=getContentResolver().openInputStream(data.getData());OutputStream out=getContentResolver().openOutputStream(ocrPhoto,"w")){
          if(in==null||out==null)return false;
          byte[] buffer=new byte[8192];int n;while((n=in.read(buffer))!=-1)out.write(buffer,0,n);
        }
      }else{
        Object thumbnail=data.getExtras()==null?null:data.getExtras().get("data");
        if(!(thumbnail instanceof Bitmap))return false;
        try(OutputStream out=getContentResolver().openOutputStream(ocrPhoto,"w")){
          if(out==null||!((Bitmap)thumbnail).compress(Bitmap.CompressFormat.JPEG,90,out))return false;
        }
      }
      return photoHasPixels(ocrPhoto);
    }catch(Exception ignored){return false;}
  }
  private void deliverOcrError(String message){
    try{queuePhotoEvent("error",ocrRunId,ocrGroupId,new JSONObject().put("error",message));}catch(Exception ignored){}
  }
  private void queuePhotoEvent(String type,String runId,String groupId,JSONObject payload){
    try{
      String id=java.util.UUID.randomUUID().toString();
      JSONObject event=new JSONObject().put("eventId",id).put("type",type).put("runId",runId).put("groupId",groupId).put("payload",payload);
      if(!getSharedPreferences("photo_events",MODE_PRIVATE).edit().putString(id,event.toString()).commit())return;
      runOnUiThread(()->deliverPhotoEvents());
    }catch(Exception ignored){}
  }
  private void deliverPhotoEvents(){
    if(!webReady||web==null)return;
    for(Object value:getSharedPreferences("photo_events",MODE_PRIVATE).getAll().values()){
      try{String event=new JSONObject((String)value).toString();web.evaluateJavascript("if(window.onNativePhotoEvent)onNativePhotoEvent("+event+")",null);}catch(Exception ignored){}
    }
  }
  private void storeAttachedPhoto(){
    final Uri photo=ocrPhoto;final String runId=ocrRunId,groupId=ocrGroupId;
    final boolean saved=ocrFromCamera,auto=ocrAutoRecognize;
    cloudExecutor.execute(()->{
      try{
        String id=PhotoStore.save(MainActivity.this,photo);
        JSONObject payload=new JSONObject().put("id",id).put("createdAt",System.currentTimeMillis()).put("photoSaved",saved).put("autoRecognize",auto);
        queuePhotoEvent("attached",runId,groupId,payload);
      }catch(Exception e){try{queuePhotoEvent("error",runId,groupId,new JSONObject().put("error","保存照片附件失败："+e.getMessage()));}catch(Exception ignored){}}
    });
  }
  private void openTextExportPicker(){
    try{TextExport pending=new TextExport(this);Intent i=new Intent(Intent.ACTION_CREATE_DOCUMENT);i.addCategory(Intent.CATEGORY_OPENABLE);i.setType(pending.type());i.putExtra(Intent.EXTRA_TITLE,pending.name());try{startActivityForResult(i,CREATE_DOC);}catch(android.content.ActivityNotFoundException missing){i.setType("application/octet-stream");startActivityForResult(i,CREATE_DOC);}}
    catch(Exception e){new TextExport(this).clear();textExportResult("无法打开保存页面："+e.getMessage(),false);}
  }
  private void textExportResult(String message,boolean ok){
    try{queueBackupEvent("text-export",new JSONObject().put("message",message).put("ok",ok));}catch(Exception ignored){}
    runOnUiThread(()->{MainActivity current=activeBackupActivity;if(current!=null)Toast.makeText(current,message,Toast.LENGTH_LONG).show();});
  }
  private void startTextExport(Uri selected,boolean local)throws Exception{
    textExportNativeWorking=true;
    try{new TextExport(this).markWriting();fileExecutor.execute(()->{
      Uri target=selected;String message;boolean ok;
      try{TextExport pending=new TextExport(MainActivity.this);if(local)target=LocalExport.create(MainActivity.this,pending.name(),pending.type());long size=pending.write(target);if(local)LocalExport.publish(MainActivity.this,target);message="文件已保存并校验（"+size+" 字节）"+(local?"\n位置：下载/沥青实验记录":"");ok=true;}catch(Exception e){if(local)LocalExport.discard(MainActivity.this,target);message="导出失败："+e.getMessage();ok=false;}
      new TextExport(MainActivity.this).clear();textExportNativeWorking=false;textExportResult(message,ok);
    });}catch(Exception e){new TextExport(this).clear();textExportNativeWorking=false;throw e;}
  }
  private void notifyWallpaper(String kind,String message){
    runOnUiThread(()->{if(web!=null)try{web.evaluateJavascript("if(window.onWallpaperEvent)onWallpaperEvent("+new JSONObject().put("kind",kind).put("message",message).toString()+")",null);}catch(Exception ignored){}});
  }
  @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);
    if(request==WALLPAPER_PICK){
      if(result==RESULT_OK&&data!=null&&data.getData()!=null){final Uri selected=data.getData();final String kind=wallpaperPickKind;
        fileExecutor.execute(()->{try{if(kind.equals("mpkg"))WallpaperStore.importMpkg(MainActivity.this,selected);else WallpaperStore.save(MainActivity.this,selected,kind);notifyWallpaper("saved","");}catch(Exception e){notifyWallpaper("error",e.getMessage());}});
      }else notifyWallpaper("cancelled","");return;
    }
    if(request==BACKUP_SAVE||request==BACKUP_OPEN){
      if(result!=RESULT_OK||data==null||data.getData()==null){endBackup("cancelled","已取消");return;}
      backupNativeWorking=true;setBackupState(request==BACKUP_SAVE?"export-writing":"restore-loading");final Uri uri=data.getData();fileExecutor.execute(()->{try{BackupArchive archive=new BackupArchive(MainActivity.this);if(request==BACKUP_SAVE){long size=archive.export(uri);setBackupState("");queueBackupEvent("saved",new JSONObject().put("message","ZIP 备份已保存并校验（"+size+" 字节）"));}else{JSONObject info=archive.restore(uri);setBackupState("restore-ready");queueBackupEvent("restore-ready",info);}}catch(Exception e){endBackup("error",e.getMessage());}finally{backupNativeWorking=false;}});return;
    }
    if(request==PHOTO_CAPTURE){
      if(recoverCameraPhoto(data)){storeAttachedPhoto();}
      else{discardPendingCameraPhoto();deliverOcrError(result==RESULT_OK?"相机没有写入有效照片，请重拍或从相册选取":"已取消拍照，未保存照片");}
      return;
    }
    if(request==PHOTO_PICK){if(result==RESULT_OK&&data!=null&&data.getData()!=null){ocrPhoto=data.getData();ocrFromCamera=false;storeAttachedPhoto();}else deliverOcrError("已取消选照片");return;}
    if(request==PICK_FILE){if(fileCallback!=null){Uri[] files=null;if(result==RESULT_OK&&data!=null){if(data.getClipData()!=null){android.content.ClipData clip=data.getClipData();files=new Uri[clip.getItemCount()];for(int i=0;i<files.length;i++)files[i]=clip.getItemAt(i).getUri();}else if(data.getData()!=null)files=new Uri[]{data.getData()};}fileCallback.onReceiveValue(files);fileCallback=null;}return;}
    if(request==CREATE_DOC){
      if(result!=RESULT_OK||data==null||data.getData()==null){new TextExport(this).clear();textExportResult("已取消导出",false);return;}
      try{startTextExport(data.getData(),false);}catch(Exception e){textExportResult("导出失败："+e.getMessage(),false);}return;
    }
  }
  @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);if(focused&&web!=null)preferSmoothRefresh();}
  @Override protected void onSaveInstanceState(Bundle out){if(ocrPhoto!=null)out.putString("ocrPhoto",ocrPhoto.toString());out.putString("ocrRunId",ocrRunId);out.putString("ocrGroupId",ocrGroupId);out.putString("ocrRunName",ocrRunName);out.putBoolean("ocrFromCamera",ocrFromCamera);out.putBoolean("ocrAutoRecognize",ocrAutoRecognize);super.onSaveInstanceState(out);}
  @Override protected void onDestroy(){if(activeBackupActivity==this)activeBackupActivity=null;webReady=false;cloudExecutor.shutdown();super.onDestroy();}
  @Override protected void onPause(){activityResumed=false;wallpaperResumeToken++;if(web!=null){web.evaluateJavascript("if(window.setWallpaperAppBackground)setWallpaperAppBackground(true)",null);web.onPause();}super.onPause();}
  @Override protected void onResume(){
    super.onResume();activityResumed=true;final int token=++wallpaperResumeToken;
    if(web!=null){
      web.onResume();preferSmoothRefresh();web.evaluateJavascript("if(window.setWallpaperAppBackground)setWallpaperAppBackground(false)",null);
      web.postDelayed(()->{if(!activityResumed||token!=wallpaperResumeToken||web==null)return;web.evaluateJavascript("if(window.setWallpaperAppBackground)setWallpaperAppBackground(false);if(window.rescheduleActiveAlarms)rescheduleActiveAlarms();if(window.refreshAfterResume)refreshAfterResume()",null);},400);
      web.postDelayed(()->{if(activityResumed&&token==wallpaperResumeToken){deliverPhotoEvents();deliverBackupEvents();}},500);
    }
  }
  @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] results){super.onRequestPermissionsResult(request,permissions,results);if(request==PHOTO_STORAGE_PERMISSION){if(results.length>0&&results[0]==PackageManager.PERMISSION_GRANTED)startOcrCamera(ocrRunId,ocrGroupId,ocrRunName);else deliverOcrError("保存到相册需要存储权限");return;}if(request==1){Toast.makeText(this,results.length>0&&results[0]==PackageManager.PERMISSION_GRANTED?"通知权限已开启":"通知权限未开启，计时提醒无法显示",Toast.LENGTH_LONG).show();if(web!=null)web.postDelayed(()->web.evaluateJavascript("if(window.render)render()",null),250);}}
  @Override public boolean dispatchKeyEvent(KeyEvent event){
    int key=event.getKeyCode();
    if(key==KeyEvent.KEYCODE_VOLUME_UP||key==KeyEvent.KEYCODE_VOLUME_DOWN){
      if(event.getAction()==KeyEvent.ACTION_DOWN&&event.getRepeatCount()==0&&web!=null)
        web.evaluateJavascript("if(window.switchSection) switchSection("+(key==KeyEvent.KEYCODE_VOLUME_UP?-1:1)+")",null);
      return true;
    }
    return super.dispatchKeyEvent(event);
  }
  @Override public void onBackPressed(){
    if(web==null){super.onBackPressed();return;}
    web.evaluateJavascript("window.handleAppBack ? window.handleAppBack() : false", value->{if(!"true".equals(value))MainActivity.super.onBackPressed();});
  }
}
