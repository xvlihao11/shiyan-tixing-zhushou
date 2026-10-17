package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.util.Base64;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/** Pending exports survive Android recreating the activity while the system picker is open. */
final class TextExport {
  private final Context context;
  private final SharedPreferences prefs;
  private final File file;
  TextExport(Context c){context=c.getApplicationContext();prefs=context.getSharedPreferences("text_export",Context.MODE_PRIVATE);file=new File(context.getFilesDir(),"pending_text_export.bin");}
  boolean busy(){return !prefs.getString("state","").isEmpty();}
  synchronized void start(String name,String type)throws Exception{
    if(busy())throw new Exception("已有文件正在导出，请完成保存或取消后重试");
    if(name==null||name.trim().isEmpty())throw new Exception("文件名为空");
    try(FileOutputStream out=new FileOutputStream(file)){out.getFD().sync();}
    if(!prefs.edit().putString("state","receiving").putString("name",name.replace('/','_').replace('\\','_')).putString("type",type==null?"application/octet-stream":type).commit())throw new Exception("无法保存导出状态");
  }
  synchronized void append(String encoded)throws Exception{
    if(!"receiving".equals(prefs.getString("state","")))throw new Exception("导出状态无效");
    if(encoded.length()>131072)throw new Exception("导出分块过大");
    byte[] bytes=Base64.decode(encoded,Base64.NO_WRAP);
    if(bytes.length>98304||file.length()+bytes.length>80L*1024*1024)throw new Exception("导出文件过大");
    try(FileOutputStream out=new FileOutputStream(file,true)){out.write(bytes);}
  }
  synchronized void finish()throws Exception{
    if(!"receiving".equals(prefs.getString("state",""))||!file.isFile()||file.length()==0)throw new Exception("待导出文件为空，不能保存");
    try(FileOutputStream out=new FileOutputStream(file,true)){out.getFD().sync();}
    if(!prefs.edit().putString("state","ready").commit())throw new Exception("无法保存导出状态");
  }
  void prepare(String name,String text,String type)throws Exception{
    start(name,type);try{byte[] bytes=text.getBytes(StandardCharsets.UTF_8);for(int i=0;i<bytes.length;i+=98304){int n=Math.min(98304,bytes.length-i);append(Base64.encodeToString(bytes,i,n,Base64.NO_WRAP));}finish();}catch(Exception e){clear();throw e;}
  }
  String name(){return prefs.getString("name","沥青实验记录.json");}
  String type(){return prefs.getString("type","application/octet-stream");}
  boolean receiving(){return "receiving".equals(prefs.getString("state",""));}
  boolean writing(){return "writing".equals(prefs.getString("state",""));}
  void markWriting()throws Exception{if(!"ready".equals(prefs.getString("state",""))||!prefs.edit().putString("state","writing").commit())throw new Exception("导出状态无法保存");}
  long write(Uri uri)throws Exception{
    if(!("ready".equals(prefs.getString("state",""))||writing())||!file.isFile())throw new Exception("待导出内容缺失，请重新导出");
    if(uri==null)throw new Exception("没有选择保存位置");
    return VerifiedFileWriter.copy(context,file,uri);
  }
  synchronized void clear(){file.delete();prefs.edit().clear().commit();}
}
