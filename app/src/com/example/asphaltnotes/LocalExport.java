package com.example.asphaltnotes.cloudtrial;
import android.content.Context;
import android.content.ContentValues;
import android.net.Uri;
import android.provider.MediaStore;
import java.io.IOException;

/** App-owned downloads stay pending until bytes have been written and verified. */
final class LocalExport {
  static Uri create(Context context,String name,String type)throws Exception{
    ContentValues values=new ContentValues();values.put(MediaStore.MediaColumns.DISPLAY_NAME,name);values.put(MediaStore.MediaColumns.MIME_TYPE,type);values.put(MediaStore.MediaColumns.RELATIVE_PATH,"Download/沥青实验记录");values.put(MediaStore.MediaColumns.IS_PENDING,1);
    Uri uri=context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI,values);if(uri==null)throw new IOException("无法在本机下载目录创建文件");return uri;
  }
  static void publish(Context context,Uri uri)throws Exception{
    ContentValues values=new ContentValues();values.put(MediaStore.MediaColumns.IS_PENDING,0);if(context.getContentResolver().update(uri,values,null,null)<=0)throw new IOException("文件已写入，但无法完成本机下载保存");
  }
  static void discard(Context context,Uri uri){if(uri!=null)try{context.getContentResolver().delete(uri,null,null);}catch(Exception ignored){}}
}
