package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.net.Uri;
import android.util.Base64;
import org.json.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;

/** Only explicitly supplied app data and referenced compact photos enter the archive. */
final class BackupArchive {
  static final long EACH=80L*1024*1024,TOTAL=500L*1024*1024;
  private final Context context;
  private final File stage,prepared;
  BackupArchive(Context c){context=c;stage=new File(c.getFilesDir(),"full_backup_stage");prepared=new File(c.getFilesDir(),"pending_full_backup.zip");}
  private File path(String name)throws Exception{
    if(!name.matches("(db\\.json|docindex\\.json|documents/[0-9]+\\.bin|photos/[a-f0-9-]{36}\\.jpg|manifest\\.json)"))throw new IOException("备份包含不允许的文件路径");
    File f=new File(stage,name);if(!f.getCanonicalPath().startsWith(stage.getCanonicalPath()+File.separator))throw new IOException("文件路径无效");return f;
  }
  private static void delete(File f){File[] fs=f.listFiles();if(fs!=null)for(File x:fs)delete(x);f.delete();}
  synchronized void clear(){delete(stage);prepared.delete();new File(prepared.toString()+".tmp").delete();}
  synchronized void begin()throws Exception{clear();if(!stage.mkdirs())throw new IOException("无法创建备份临时目录");}
  synchronized void append(String name,String encoded,boolean first)throws Exception{
    if(encoded.length()>131072)throw new IOException("传输分块太大");
    File f=path(name);f.getParentFile().mkdirs();byte[] bytes=Base64.decode(encoded,Base64.NO_WRAP);
    if((first?0:f.length())+bytes.length>EACH)throw new IOException("单个文件超过80MB");
    try(FileOutputStream out=new FileOutputStream(f,!first)){out.write(bytes);}
    if(total(stage)>TOTAL)throw new IOException("备份超过500MB");
  }
  static long total(File f){if(f.isFile())return f.length();long n=0;File[] fs=f.listFiles();if(fs!=null)for(File x:fs)n+=total(x);return n;}
  private static String hash(File f)throws Exception{MessageDigest md=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(f)){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)md.update(b,0,n);}StringBuilder s=new StringBuilder();for(byte b:md.digest())s.append(String.format(java.util.Locale.US,"%02x",b&255));return s.toString();}
  private static void copy(File from,File to)throws Exception{to.getParentFile().mkdirs();try(InputStream in=new FileInputStream(from);FileOutputStream out=new FileOutputStream(to)){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)out.write(b,0,n);out.getFD().sync();}}
  private static String read(File f)throws Exception{if(f.length()>EACH)throw new IOException("文件过大");ByteArrayOutputStream out=new ByteArrayOutputStream();try(InputStream in=new FileInputStream(f)){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}return out.toString("UTF-8");}
  private void files(File dir,List<File> list){File[] fs=dir.listFiles();if(fs!=null)for(File f:fs)if(f.isDirectory())files(f,list);else list.add(f);}
  private String relative(File f){return stage.toURI().relativize(f.toURI()).getPath();}
  synchronized void prepare(String photoIds,boolean photos)throws Exception{
    JSONArray ids=new JSONArray(photoIds);Set<String> seen=new HashSet<>();
    if(photos)for(int i=0;i<ids.length();i++){String id=ids.getString(i);if(seen.add(id))copy(PhotoStore.ensureCompact(context,id),path("photos/"+id+".jpg"));}
    if(total(stage)>TOTAL)throw new IOException("备份超过500MB");
    validateData();JSONArray entries=new JSONArray();List<File> fs=new ArrayList<>();files(stage,fs);
    for(File f:fs)entries.put(new JSONObject().put("path",relative(f)).put("size",f.length()).put("sha256",hash(f)));
    JSONObject manifest=new JSONObject().put("format","asphalt-full-backup").put("version",1).put("created",System.currentTimeMillis()).put("photosIncluded",photos).put("entries",entries);
    try(FileOutputStream out=new FileOutputStream(path("manifest.json"))){out.write(manifest.toString().getBytes("UTF-8"));}
    File tmp=new File(prepared.toString()+".tmp");
    try(FileOutputStream output=new FileOutputStream(tmp);ZipOutputStream zip=new ZipOutputStream(output)){
      fs.clear();files(stage,fs);byte[] buffer=new byte[65536];for(File f:fs){zip.putNextEntry(new ZipEntry(relative(f)));try(InputStream in=new FileInputStream(f)){int n;while((n=in.read(buffer))!=-1)zip.write(buffer,0,n);}zip.closeEntry();}zip.finish();output.getFD().sync();
    }
    if(tmp.length()==0||!tmp.renameTo(prepared))throw new IOException("备份文件生成失败");
    try(ZipFile check=new ZipFile(prepared)){if(check.getEntry("db.json")==null||check.getEntry("manifest.json")==null)throw new IOException("备份结构不完整");}
  }
  synchronized long export(Uri uri)throws Exception{
    try{return VerifiedFileWriter.copy(context,prepared,uri);}finally{clear();}
  }
  synchronized JSONObject restore(Uri uri)throws Exception{
    begin();try{
      Set<String> names=new HashSet<>();long total=0;
      try(InputStream input=context.getContentResolver().openInputStream(uri);ZipInputStream zip=new ZipInputStream(input)){
        ZipEntry e;byte[] b=new byte[32768];while((e=zip.getNextEntry())!=null){String name=e.getName();File f=path(name);if(e.isDirectory()||!names.add(name)||names.size()>20000)throw new IOException("重复或无效的备份条目");f.getParentFile().mkdirs();long count=0;try(FileOutputStream out=new FileOutputStream(f)){int n;while((n=zip.read(b))!=-1){count+=n;total+=n;if(count>EACH||total>TOTAL)throw new IOException("备份超出大小限制");out.write(b,0,n);}}zip.closeEntry();}
      }
      if(names.isEmpty())throw new IOException("备份为空或不是有效 ZIP。0 B 文件没有可恢复的数据，请重新导出");
      JSONObject manifest=new JSONObject(read(path("manifest.json")));if(!manifest.optString("format").equals("asphalt-full-backup")||manifest.optInt("version")!=1)throw new IOException("不支持的备份格式");
      JSONArray entries=manifest.getJSONArray("entries");Set<String> expected=new HashSet<>();expected.add("manifest.json");
      for(int i=0;i<entries.length();i++){JSONObject e=entries.getJSONObject(i);String name=e.getString("path");if(name.equals("manifest.json")||!expected.add(name))throw new IOException("备份清单无效");File f=path(name);if(!f.isFile()||f.length()!=e.getLong("size")||!hash(f).equals(e.getString("sha256")))throw new IOException("备份校验失败："+name);}
      if(!expected.equals(names))throw new IOException("备份条目与清单不一致");validateData();
      JSONObject db=new JSONObject(read(path("db.json")));Set<String> refs=new HashSet<>(photoIds(db));
      if(manifest.optBoolean("photosIncluded"))for(String id:refs)if(!path("photos/"+id+".jpg").isFile())throw new IOException("备份缺少照片："+id);
      JSONArray index=new JSONArray(read(path("docindex.json")));Set<String> allowed=new HashSet<>();allowed.add("manifest.json");allowed.add("db.json");allowed.add("docindex.json");for(int i=0;i<index.length();i++)allowed.add("documents/"+i+".bin");if(manifest.optBoolean("photosIncluded"))for(String id:refs)allowed.add("photos/"+id+".jpg");if(!allowed.equals(names))throw new IOException("备份含未引用的文件");for(String name:names)if(name.startsWith("photos/")){android.graphics.BitmapFactory.Options bounds=new android.graphics.BitmapFactory.Options();bounds.inJustDecodeBounds=true;android.graphics.BitmapFactory.decodeFile(path(name).toString(),bounds);if(bounds.outWidth<=0||bounds.outHeight<=0)throw new IOException("照片附件无效");}
      return new JSONObject().put("documents",new JSONArray(read(path("docindex.json"))).length()).put("photosIncluded",manifest.optBoolean("photosIncluded"));
    }catch(Exception e){clear();throw e;}
  }
  private static List<String> photoIds(JSONObject db)throws Exception{List<String> result=new ArrayList<>();JSONArray runs=db.getJSONArray("runs");for(int i=0;i<runs.length();i++){JSONArray gs=runs.getJSONObject(i).optJSONArray("groups");if(gs==null)continue;for(int j=0;j<gs.length();j++){JSONArray ps=gs.getJSONObject(j).optJSONArray("photos");if(ps!=null)for(int k=0;k<ps.length();k++){String id=ps.getJSONObject(k).getString("id");if(!id.matches("[a-f0-9-]{36}"))throw new IOException("照片编号无效");result.add(id);}}}return result;}
  private void validateData()throws Exception{
    JSONObject db=new JSONObject(read(path("db.json")));for(String key:new String[]{"templates","samples","runs"}){JSONArray a=db.getJSONArray(key);Set<String> ids=new HashSet<>();for(int i=0;i<a.length();i++){String id=a.getJSONObject(i).getString("id");if(id.isEmpty()||!ids.add(id))throw new IOException("数据编号重复或无效");}}
    JSONArray docs=new JSONArray(read(path("docindex.json")));Set<String> ids=new HashSet<>();
    for(int i=0;i<docs.length();i++){JSONObject d=docs.getJSONObject(i);String id=d.getString("id"),type=d.getString("type");if(id.isEmpty()||!ids.add(id)||!(type.equals("md")||type.equals("pdf")))throw new IOException("文档元数据无效");d.getString("name");d.getString("text");d.getJSONArray("pages");d.getJSONArray("outline");d.getLong("created");File f=path("documents/"+i+".bin");if(!f.isFile()||f.length()!=d.getLong("size"))throw new IOException("文档字节缺失或大小不符");}
  }
  synchronized JSONObject info(String name)throws Exception{File f=path(name);if(!f.isFile())throw new IOException("文件不存在");return new JSONObject().put("size",f.length());}
  synchronized String chunk(String name,long offset)throws Exception{File f=path(name);if(offset<0||offset>f.length())throw new IOException("偏移无效");try(RandomAccessFile in=new RandomAccessFile(f,"r")){in.seek(offset);byte[] b=new byte[(int)Math.min(98304,f.length()-offset)];in.readFully(b);return Base64.encodeToString(b,Base64.NO_WRAP);}}
  synchronized void rollbackPhotos()throws Exception{File marker=new File(stage,"photos-commit-started");if(!marker.isFile())return;File[] fs=new File(stage,"photos").listFiles();if(fs!=null)for(File f:fs){File target=PhotoStore.file(context,f.getName().substring(0,36),"compact"),old=new File(stage,"rollback/"+f.getName());if(old.isFile())copy(old,target);else target.delete();}marker.delete();}
  synchronized void commitPhotos()throws Exception{
    File dir=new File(stage,"photos");File[] fs=dir.listFiles();if(fs==null)return;
    // Snapshot every destination before the marker makes recovery eligible.
    for(File f:fs){File target=PhotoStore.file(context,f.getName().substring(0,36),"compact"),old=new File(stage,"rollback/"+f.getName());if(target.exists())copy(target,old);}
    try(FileOutputStream marker=new FileOutputStream(new File(stage,"photos-commit-started"))){marker.write(1);marker.getFD().sync();}
    List<File> committed=new ArrayList<>();try{for(File f:fs){String id=f.getName().substring(0,36);File target=PhotoStore.file(context,id,"compact"),old=new File(stage,"rollback/"+f.getName());android.graphics.BitmapFactory.Options bounds=new android.graphics.BitmapFactory.Options();bounds.inJustDecodeBounds=true;android.graphics.BitmapFactory.decodeFile(f.toString(),bounds);if(bounds.outWidth<=0||bounds.outHeight<=0)throw new IOException("照片附件无效");copy(f,new File(target.toString()+".restore"));File temp=new File(target.toString()+".restore");if(!temp.renameTo(target))throw new IOException("照片恢复失败");committed.add(target);}}
    catch(Exception e){for(File target:committed){File old=new File(stage,"rollback/"+target.getName().replace("_compact.jpg",".jpg"));if(old.exists())copy(old,target);else target.delete();}throw e;}
  }
}
