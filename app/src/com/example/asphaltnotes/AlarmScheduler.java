package com.example.asphaltnotes.cloudtrial;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import org.json.JSONObject;
import java.util.Map;

/** Persistent, one-shot reminders that Android can deliver without the WebView running. */
final class AlarmScheduler {
  private static final String PENDING="pending_reminders";
  private static final String FIRED="fired_reminders";
  private static final String DIAGNOSTICS="reminder_diagnostics";
  private static final String PREFIX="asphalt.reminder.";
  private AlarmScheduler(){}

  private static PendingIntent delivery(Context context,String id,long at,String title,String message,boolean ring,int flags){
    Intent i=new Intent(context,ReminderReceiver.class);
    i.setAction(PREFIX+id);
    i.putExtra("due_at",at);
    i.putExtra("notification_id",id.hashCode());
    i.putExtra("title",title);
    i.putExtra("message",message);
    i.putExtra("ring",ring);
    return PendingIntent.getBroadcast(context,id.hashCode(),i,flags|PendingIntent.FLAG_IMMUTABLE);
  }

  // Remove the former release's activity alarm and duplicate receiver fallback on upgrade.
  private static void cancelLegacyActivity(Context context,String id){
    Intent i=new Intent(context,AlarmActivity.class);
    i.setAction(PREFIX+id);
    PendingIntent old=PendingIntent.getActivity(context,id.hashCode(),i,PendingIntent.FLAG_NO_CREATE|PendingIntent.FLAG_IMMUTABLE);
    if(old!=null){((AlarmManager)context.getSystemService(Context.ALARM_SERVICE)).cancel(old);old.cancel();}
  }

  static synchronized boolean claim(Context context,Intent incoming){
    String action=incoming.getAction();
    if(action==null||!action.startsWith(PREFIX))return false;
    String id=action.substring(PREFIX.length());
    String raw=context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).getString(id,null);
    if(raw==null)return false;
    try{
      long due=new JSONObject(raw).getLong("at");
      if(due!=incoming.getLongExtra("due_at",0)||System.currentTimeMillis()+30000<due)return false;
    }catch(Exception e){return false;}
    SharedPreferences fired=context.getSharedPreferences(FIRED,Context.MODE_PRIVATE);
    if(fired.getBoolean(action,false)||!fired.edit().putBoolean(action,true).commit())return false;
    received(context,id);
    return true;
  }

  static synchronized boolean schedule(Context context,String id,long at,String title,String message,boolean ring){
    if(id==null||id.isEmpty()||at<=System.currentTimeMillis())return false;
    try{
      AlarmManager alarms=(AlarmManager)context.getSystemService(Context.ALARM_SERVICE);
      if(Build.VERSION.SDK_INT>=31&&!alarms.canScheduleExactAlarms()){
        failed(context,"exact_alarm_permission_missing");return false;
      }
      cancelLegacyActivity(context,id);
      PendingIntent alarm=delivery(context,id,at,title,message,ring,PendingIntent.FLAG_UPDATE_CURRENT);
      // An alarm-clock broadcast wakes the app while it is not running and while Doze is active.
      // Both reminder modes use the same delivery path; the receiver chooses sound or notification.
      Intent open=new Intent(context,MainActivity.class);
      PendingIntent show=PendingIntent.getActivity(context,id.hashCode()+1,open,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
      alarms.setAlarmClock(new AlarmManager.AlarmClockInfo(at,show),alarm);
      JSONObject data=new JSONObject();
      data.put("at",at);data.put("title",title);data.put("message",message);data.put("ring",ring);
      if(!context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().putString(id,data.toString()).commit()){
        alarms.cancel(alarm);failed(context,"save_alarm_failed");return false;
      }
      context.getSharedPreferences(FIRED,Context.MODE_PRIVATE).edit().remove(PREFIX+id).commit();
      context.getSharedPreferences(DIAGNOSTICS,Context.MODE_PRIVATE).edit()
        .putLong("last_scheduled_at",System.currentTimeMillis())
        .putLong("last_due_at",at).putString("last_error","").apply();
      return true;
    }catch(Exception e){
      context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().remove(id).apply();
      failed(context,"schedule_"+e.getClass().getSimpleName());
      return false;
    }
  }

  static synchronized boolean isScheduled(Context context,String id,long at,boolean ring){
    try{
      String raw=context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).getString(id,null);
      if(raw==null)return false;
      JSONObject data=new JSONObject(raw);
      if(data.getLong("at")!=at||data.optBoolean("ring")!=ring||at<=System.currentTimeMillis())return false;
      return delivery(context,id,at,"","",ring,PendingIntent.FLAG_NO_CREATE)!=null;
    }catch(Exception e){return false;}
  }

  static synchronized void cancel(Context context,String id){
    context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().remove(id).apply();
    PendingIntent alarm=delivery(context,id,0,"","",false,PendingIntent.FLAG_NO_CREATE);
    if(alarm!=null){((AlarmManager)context.getSystemService(Context.ALARM_SERVICE)).cancel(alarm);alarm.cancel();}
    cancelLegacyActivity(context,id);
  }

  static synchronized void received(Context context,String id){
    context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().remove(id).commit();
    context.getSharedPreferences(DIAGNOSTICS,Context.MODE_PRIVATE).edit()
      .putLong("last_triggered_at",System.currentTimeMillis()).putString("last_error","").apply();
  }

  static synchronized void restore(Context context){
    for(Map.Entry<String,?> entry:context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).getAll().entrySet()){
      try{
        JSONObject d=new JSONObject((String)entry.getValue());
        String id=entry.getKey();
        if(d.getLong("at")<=System.currentTimeMillis()){
          // A previous alarm may already have fired; never generate a late duplicate.
          context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().remove(id).apply();
          continue;
        }
        schedule(context,id,d.getLong("at"),d.optString("title"),d.optString("message"),d.optBoolean("ring"));
      }catch(Exception e){
        context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).edit().remove(entry.getKey()).apply();
      }
    }
  }

  static void failed(Context context,String reason){
    context.getSharedPreferences(DIAGNOSTICS,Context.MODE_PRIVATE).edit().putString("last_error",reason).apply();
  }

  static String diagnostics(Context context){
    SharedPreferences p=context.getSharedPreferences(DIAGNOSTICS,Context.MODE_PRIVATE);
    try{
      JSONObject d=new JSONObject();
      d.put("pending",context.getSharedPreferences(PENDING,Context.MODE_PRIVATE).getAll().size());
      d.put("lastTriggeredAt",p.getLong("last_triggered_at",0));
      d.put("lastScheduledAt",p.getLong("last_scheduled_at",0));
      d.put("lastDueAt",p.getLong("last_due_at",0));
      d.put("lastError",p.getString("last_error",""));
      return d.toString();
    }catch(Exception e){return "{}";}
  }
}
