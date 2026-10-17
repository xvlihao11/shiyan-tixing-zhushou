package com.example.asphaltnotes.cloudtrial;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.RingtoneManager;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** A visible, short-lived alarm that can ring independently of channel sound. */
public class AlarmActivity extends Activity {
  private MediaPlayer player;
  private ToneGenerator fallback;
  private final Handler handler=new Handler(Looper.getMainLooper());
  private final Runnable timeout=()->finish();
  private final Runnable pulse=new Runnable(){@Override public void run(){
    if(fallback!=null){fallback.startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD,800);handler.postDelayed(this,1800);}
  }};

  @Override public void onCreate(Bundle state){
    super.onCreate(state);
    if(Build.VERSION.SDK_INT>=27){setShowWhenLocked(true);setTurnScreenOn(true);}
    setVolumeControlStream(AudioManager.STREAM_ALARM);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
      |WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
      |WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
    Intent alarm=getIntent();
    if(!AlarmScheduler.claim(this,alarm)){finish();return;}
    String title=alarm.getStringExtra("title");
    String message=alarm.getStringExtra("message");
    if(title==null)title="沥青实验记录";
    if(message==null)message="计时已到";
    showAlarm(title,message);
    startSound();
    handler.postDelayed(timeout,30000);
  }

  private void showAlarm(String title,String message){
    LinearLayout panel=new LinearLayout(this);
    panel.setOrientation(LinearLayout.VERTICAL);
    panel.setGravity(Gravity.CENTER);
    panel.setPadding(28,36,28,36);
    panel.setBackgroundColor(0xfff4f8f9);
    TextView heading=new TextView(this);
    heading.setText("实验计时已到");
    heading.setTextSize(28);
    heading.setTextColor(0xff123e48);
    heading.setGravity(Gravity.CENTER);
    panel.addView(heading);
    TextView detail=new TextView(this);
    detail.setText(title+"\n"+message);
    detail.setTextSize(18);
    detail.setTextColor(0xff315c66);
    detail.setPadding(0,24,0,30);
    detail.setGravity(Gravity.CENTER);
    panel.addView(detail);
    Button stop=new Button(this);
    stop.setText("停止闹铃");
    stop.setOnClickListener(v->finish());
    panel.addView(stop,new LinearLayout.LayoutParams(-1,-2));
    setContentView(panel);
    // The activity itself is visible on the lock screen; the notification keeps the event in history.
    try{
      NotificationManager manager=(NotificationManager)getSystemService(Context.NOTIFICATION_SERVICE);
      Notification.Builder builder=new Notification.Builder(this,"ring_v3")
        .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
        .setContentTitle(title).setContentText(message)
        .setCategory(Notification.CATEGORY_ALARM)
        .setVisibility(Notification.VISIBILITY_PUBLIC)
        .setAutoCancel(true)
        .setOnlyAlertOnce(true);
      manager.notify(getIntent().getIntExtra("notification_id",0),builder.build());
    }catch(Exception e){AlarmScheduler.failed(this,"notification_"+e.getClass().getSimpleName());}
  }

  private void startSound(){
    try{
      player=new MediaPlayer();
      player.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM)
        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build());
      android.net.Uri tone=RingtoneManager.getActualDefaultRingtoneUri(this,RingtoneManager.TYPE_ALARM);
      if(tone==null)tone=RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM);
      player.setDataSource(this,tone);
      player.setLooping(true);
      player.prepare();
      player.start();
    }catch(Exception e){
      AlarmScheduler.failed(this,"sound_"+e.getClass().getSimpleName());
      if(player!=null){player.release();player=null;}
      try{fallback=new ToneGenerator(AudioManager.STREAM_ALARM,100);handler.post(pulse);}
      catch(Exception ignored){AlarmScheduler.failed(this,"sound_and_tone_unavailable");}
    }
  }

  @Override protected void onDestroy(){
    handler.removeCallbacks(timeout);
    handler.removeCallbacks(pulse);
    if(player!=null){if(player.isPlaying())player.stop();player.release();player=null;}
    if(fallback!=null){fallback.release();fallback=null;}
    super.onDestroy();
  }
}
