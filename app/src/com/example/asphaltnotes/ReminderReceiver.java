package com.example.asphaltnotes.cloudtrial;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.Manifest;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.media.MediaPlayer;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.provider.Settings;

public class ReminderReceiver extends BroadcastReceiver {
  public void onReceive(Context context, Intent intent) {
    if(!AlarmScheduler.claim(context,intent))return;
    boolean ring = intent.getBooleanExtra("ring", false);
    String channel = ring ? "ring_v3" : "notice_v3";
    NotificationManager nm = (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
    boolean canNotify=(Build.VERSION.SDK_INT<33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)==PackageManager.PERMISSION_GRANTED)
        && nm.areNotificationsEnabled();
    if(canNotify && Build.VERSION.SDK_INT >= 26){
      NotificationChannel selected=nm.getNotificationChannel(channel);
      if(selected!=null&&selected.getImportance()==NotificationManager.IMPORTANCE_NONE)canNotify=false;
    }
    if(!canNotify){
      AlarmScheduler.failed(context,"notifications_disabled");
      if(!ring)return;
    }
    if (canNotify && Build.VERSION.SDK_INT >= 26) {
      NotificationChannel nc = new NotificationChannel(channel, ring ? "实验响铃提醒" : "实验通知提醒", NotificationManager.IMPORTANCE_HIGH);
      nc.enableVibration(true);
      nc.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC);
      if(ring) nc.setSound(android.provider.Settings.System.DEFAULT_ALARM_ALERT_URI, new android.media.AudioAttributes.Builder().setUsage(android.media.AudioAttributes.USAGE_ALARM).build());
      nm.createNotificationChannel(nc);
    }
    if(canNotify){
    Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(context, channel) : new Notification.Builder(context);
    Notification n = builder.setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
      .setContentTitle(intent.getStringExtra("title"))
      .setContentText(intent.getStringExtra("message"))
      .setVisibility(Notification.VISIBILITY_PUBLIC)
      .setCategory(Notification.CATEGORY_ALARM)
      .setPriority(Notification.PRIORITY_MAX)
      .setShowWhen(true)
      .setAutoCancel(true)
      .setContentIntent(android.app.PendingIntent.getActivity(context, 0, new Intent(context, MainActivity.class), android.app.PendingIntent.FLAG_UPDATE_CURRENT | android.app.PendingIntent.FLAG_IMMUTABLE))
      .build();
    try{nm.notify(intent.getIntExtra("notification_id", 0), n);}
    catch(SecurityException e){AlarmScheduler.failed(context,"notifications_disabled");}
    }
    if(ring){
      // The user's existing notification channel may be silent; play the alarm stream as well.
      final PendingResult result=goAsync();
      new Thread(()->{
        MediaPlayer player=null;
        ToneGenerator fallback=null;
        try{
          player=new MediaPlayer();
          player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build());
          android.net.Uri tone=android.media.RingtoneManager.getActualDefaultRingtoneUri(context,android.media.RingtoneManager.TYPE_ALARM);
          if(tone==null)tone=Settings.System.DEFAULT_ALARM_ALERT_URI;
          player.setDataSource(context,tone);
          player.prepare();player.start();
          Thread.sleep(8000);
        }catch(Exception e){
          AlarmScheduler.failed(context,"sound_"+e.getClass().getSimpleName());
          try{fallback=new ToneGenerator(AudioManager.STREAM_ALARM,100);
            for(int i=0;i<4;i++){fallback.startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD,800);Thread.sleep(1800);}
          }catch(Exception ignored){AlarmScheduler.failed(context,"sound_and_tone_unavailable");}
        }
        finally{if(player!=null)player.release();if(fallback!=null)fallback.release();result.finish();}
      },"asphalt-alarm-sound").start();
    }
  }
}
