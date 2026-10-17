package com.example.asphaltnotes.cloudtrial;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

public class BootReceiver extends BroadcastReceiver {
  @Override public void onReceive(Context context,Intent intent){
    String action=intent.getAction();
    if(Intent.ACTION_BOOT_COMPLETED.equals(action)
        || Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)
        || (Build.VERSION.SDK_INT>=31 && "android.app.action.SCHEDULE_EXACT_ALARM_PERMISSION_STATE_CHANGED".equals(action))){
      AlarmScheduler.restore(context);
    }
  }
}
