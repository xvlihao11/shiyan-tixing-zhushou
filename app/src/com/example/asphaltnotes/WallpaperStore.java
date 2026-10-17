package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.BitmapFactory;
import android.graphics.Bitmap;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.webkit.WebResourceResponse;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.*;
import java.util.*;

/** App-private wallpaper gallery; never changes source media or experiment photos. */
final class WallpaperStore {
  private static SharedPreferences prefs(Context c){return c.getSharedPreferences("ui_wallpaper",0);}
  private static File galleryDir(Context c){File dir=new File(c.getFilesDir(),"ui_wallpaper_gallery");dir.mkdirs();return dir;}
  private static File media(Context c,String id){return "legacy".equals(id)?new File(c.getFilesDir(),"ui_wallpaper.bin"):new File(galleryDir(c),id+".bin");}
  private static File thumbnailFile(Context c,String id){return new File(galleryDir(c),id+".jpg");}
  private static File storeThumbnail(Context c,String id,Bitmap frame) throws Exception {
    File cover=thumbnailFile(c,id),pending=new File(galleryDir(c),id+".jpg.pending");
    double ratio=Math.min(1.0,320.0/Math.max(frame.getWidth(),frame.getHeight()));
    Bitmap scaled=Bitmap.createScaledBitmap(frame,Math.max(1,(int)(frame.getWidth()*ratio)),Math.max(1,(int)(frame.getHeight()*ratio)),true);
    try {
      try(FileOutputStream out=new FileOutputStream(pending)){
        if(!scaled.compress(Bitmap.CompressFormat.JPEG,82,out))throw new IOException("封面生成失败");
        out.getFD().sync();
      }
      if(!pending.renameTo(cover))throw new IOException("封面保存失败");
      return cover;
    }finally{if(scaled!=frame)scaled.recycle();pending.delete();}
  }
  private static synchronized File videoThumbnail(Context c,String id) throws Exception {
    JSONObject selected=entry(items(c),id);
    if(selected==null||!"video".equals(selected.optString("kind"))||!media(c,id).isFile())throw new IOException("视频壁纸不存在");
    File cover=thumbnailFile(c,id);
    if(cover.isFile()&&cover.length()>0)return cover;
    MediaMetadataRetriever retriever=new MediaMetadataRetriever();
    Bitmap frame=null;
    try {
      retriever.setDataSource(media(c,id).getAbsolutePath());
      frame=retriever.getFrameAtTime(0,MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
      if(frame==null)frame=retriever.getFrameAtTime(1000000,MediaMetadataRetriever.OPTION_CLOSEST_SYNC);
      if(frame==null)throw new IOException("此视频无法提取封面");
      File result=storeThumbnail(c,id,frame);
      return result;
    }finally{
      retriever.release();
      if(frame!=null)frame.recycle();
    }
  }
  private static void migrate(Context c) throws Exception {
    SharedPreferences p=prefs(c);if(p.contains("gallery_json"))return;
    JSONArray entries=new JSONArray();String kind=p.getString("kind","");
    String active="";
    if(!kind.isEmpty()&&media(c,"legacy").isFile()){
      entries.put(new JSONObject().put("id","legacy").put("kind",kind).put("mime",p.getString("mime","image/jpeg"))
        .put("source",p.getString("source","direct")).put("name","之前导入的壁纸").put("revision",p.getLong("revision",1)));
      active="legacy";
    }
    if(!p.edit().putString("gallery_json",entries.toString()).putString("active_id",active).commit())throw new IOException("壁纸列表保存失败");
  }
  private static JSONArray items(Context c) throws Exception {migrate(c);return new JSONArray(prefs(c).getString("gallery_json","[]"));}
  private static JSONObject entry(JSONArray a,String id) throws Exception {
    for(int i=0;i<a.length();i++){JSONObject e=a.getJSONObject(i);if(id.equals(e.optString("id")))return e;}
    return null;
  }
  private static String displayName(Context c,Uri uri,String fallback){
    if("content".equals(uri.getScheme()))try(Cursor cur=c.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
      if(cur!=null&&cur.moveToFirst()){String name=cur.getString(0);if(name!=null&&!name.trim().isEmpty())return name.trim();}
    }catch(Exception ignored){}
    return fallback;
  }
  static synchronized JSONObject state(Context c) throws Exception {
    JSONArray all=items(c);String id=prefs(c).getString("active_id","");JSONObject active=entry(all,id);
    if(active!=null&&!media(c,id).isFile())active=null;
    return new JSONObject().put("kind",active==null?"":active.optString("kind"))
      .put("source",active==null?"direct":active.optString("source","direct"))
      .put("revision",active==null?0:active.optLong("revision",0))
      .put("activeId",active==null?"":id).put("mediaUrl",active==null?"":Uri.fromFile(media(c,id)).toString()).put("gallery",all).put("ok",true);
  }
  static synchronized JSONObject importMpkg(Context c,Uri source) throws Exception {
    File prepared=new File(c.getFilesDir(),"ui_wallpaper_mpkg_asset.pending");
    try {
      long sourceLength=-1;
      try(Cursor cursor=c.getContentResolver().query(source,new String[]{OpenableColumns.SIZE},null,null,null)){
        if(cursor!=null&&cursor.moveToFirst()&&!cursor.isNull(0)){long reported=cursor.getLong(0);if(reported>0)sourceLength=reported;}
      }catch(Exception ignored){}
      MpkgArchive.Choice chosen;
      try(InputStream in=c.getContentResolver().openInputStream(source)){
        if(in==null)throw new IOException("无法读取所选 MPKG");
        chosen=MpkgStream.extract(in,sourceLength,prepared);
      }
      // One extraction, then validation and atomic rename. No full-package copy,
      // second media copy, or preview decode before notifying the player.
      return savePrepared(c,prepared,chosen.kind,displayName(c,source,"MPKG 壁纸"),
        chosen.kind.equals("video")?"mpkg-video":"mpkg-preview",chosen.length);
    }finally{prepared.delete();}
  }
  static synchronized JSONObject save(Context c,Uri source,String expected) throws Exception {
    return save(c,source,expected,displayName(c,source,expected.equals("video")?"视频壁纸":"图片壁纸"),"direct");
  }
  private static synchronized JSONObject save(Context c,Uri source,String expected,String label,String sourceKind) throws Exception {
    if(!expected.equals("image")&&!expected.equals("video"))throw new IOException("请选择图片或视频");
    File tmp=new File(c.getFilesDir(),"ui_wallpaper.pending");long size=0;
    try {
      try(InputStream in=c.getContentResolver().openInputStream(source);FileOutputStream out=new FileOutputStream(tmp)){
        if(in==null)throw new IOException("无法打开所选文件");byte[] buffer=new byte[32768];int n;
        long cap=expected.equals("video")?120L*1024*1024:30L*1024*1024;
        while((n=in.read(buffer))!=-1){size+=n;if(size>cap)throw new IOException(expected.equals("video")?"视频最多 120 MB":"图片最多 30 MB");out.write(buffer,0,n);}
        out.getFD().sync();
      }
      return savePrepared(c,tmp,expected,label,sourceKind,size);
    }finally{tmp.delete();}
  }
  private static synchronized JSONObject savePrepared(Context c,File tmp,String expected,String label,String sourceKind,long size) throws Exception {
    if(!expected.equals("image")&&!expected.equals("video"))throw new IOException("请选择图片或视频");
    long cap=expected.equals("video")?120L*1024*1024:30L*1024*1024;
    if(size>cap||tmp.length()!=size)throw new IOException("壁纸素材大小无效或超出上限");
    JSONArray all=items(c);
    String id="w"+UUID.randomUUID().toString().replace("-","");File target=media(c,id);
      if(size<12)throw new IOException("文件无效");
      String mime="video/mp4";
      if(expected.equals("image")){
        byte[] head=new byte[12];try(InputStream in=new FileInputStream(tmp)){if(in.read(head)!=12)throw new IOException("图片无效");}
        if((head[0]&255)==0xff&&(head[1]&255)==0xd8)mime="image/jpeg";
        else if((head[0]&255)==137&&head[1]==80&&head[2]==78&&head[3]==71)mime="image/png";
        else if(new String(head,0,4,"US-ASCII").equals("RIFF")&&new String(head,8,4,"US-ASCII").equals("WEBP"))mime="image/webp";
        else if(new String(head,0,6,"US-ASCII").equals("GIF87a")||new String(head,0,6,"US-ASCII").equals("GIF89a"))mime="image/gif";
        else throw new IOException("请选择 JPG、PNG、WebP 或 GIF 图片");
        BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;
        BitmapFactory.decodeFile(tmp.toString(),bounds);
        if(bounds.outWidth<=0||bounds.outHeight<=0)throw new IOException("请选择有效的 JPG、PNG、WebP 或 GIF 图片");
      }else{
        byte[] head=new byte[512];int count;try(InputStream in=new FileInputStream(tmp)){count=in.read(head);}
        mime=videoMime(head,count);
        if(mime==null)throw new IOException("请选择 MP4、MOV、3GP 或 WebM 视频");
        MediaMetadataRetriever retriever=new MediaMetadataRetriever();
        try{
          retriever.setDataSource(tmp.getAbsolutePath());
          if(!"yes".equalsIgnoreCase(retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO)))
            throw new IOException("所选文件没有可读取的视频画面");
        }catch(RuntimeException e){throw new IOException("无法读取视频，请换一个视频文件",e);}
        finally{retriever.release();}
      }
      // The new file is private to the app; previously imported files remain intact.
      if(!tmp.renameTo(target))throw new IOException("壁纸保存失败");
      long revision=System.currentTimeMillis();
      all.put(new JSONObject().put("id",id).put("kind",expected).put("mime",mime).put("source",sourceKind)
        .put("name",label).put("size",size).put("revision",revision));
      if(!prefs(c).edit().putString("gallery_json",all.toString()).putString("active_id",id).commit()){
        target.delete();throw new IOException("壁纸列表保存失败");
      }
      // As in V5.44, finish importing before any on-demand gallery thumbnail decode.
      return state(c);
  }
  static String videoMime(byte[] head,int count) throws Exception {
    if(count>=12&&new String(head,4,4,"US-ASCII").equals("ftyp")){
      String brand=new String(head,8,4,"US-ASCII");
      if(brand.equals("qt  "))return "video/quicktime";
      if(brand.startsWith("3gp")||brand.startsWith("3g2"))return "video/3gpp";
      return "video/mp4";
    }
    if(count>=4&&(head[0]&255)==0x1a&&(head[1]&255)==0x45&&(head[2]&255)==0xdf&&(head[3]&255)==0xa3){
      String prefix=new String(head,0,count,"US-ASCII").toLowerCase(Locale.ROOT);
      if(prefix.contains("webm"))return "video/webm";
    }
    return null;
  }
  static synchronized JSONObject clear(Context c) throws Exception {
    items(c);
    if(!prefs(c).edit().putString("active_id","").commit())throw new IOException("恢复默认背景失败");
    return state(c);
  }
  static synchronized JSONObject activate(Context c,String id) throws Exception {
    JSONArray all=items(c);if(entry(all,id)==null||!media(c,id).isFile())throw new IOException("壁纸文件已不存在");
    if(!prefs(c).edit().putString("active_id",id).commit())throw new IOException("切换壁纸失败");
    return state(c);
  }
  static synchronized JSONObject rename(Context c,String id,String name) throws Exception {
    String label=name==null?"":name.trim();
    if(label.isEmpty()||label.length()>80||java.util.regex.Pattern.compile("[\\p{Cntrl}]").matcher(label).find())
      throw new IOException("壁纸名称应为 1 至 80 个字符，且不能含换行或控制字符");
    JSONArray all=items(c);JSONObject selected=entry(all,id);
    if(selected==null||!media(c,id).isFile())throw new IOException("壁纸文件已不存在");
    selected.put("name",label);
    if(!prefs(c).edit().putString("gallery_json",all.toString()).commit())
      throw new IOException("壁纸重命名失败");
    return state(c);
  }
  static synchronized JSONObject remove(Context c,String id) throws Exception {
    JSONArray all=items(c),updated=new JSONArray();JSONObject found=null;
    for(int i=0;i<all.length();i++){JSONObject e=all.getJSONObject(i);if(id.equals(e.optString("id")))found=e;else updated.put(e);}
    if(found==null)throw new IOException("壁纸记录不存在");
    String active=prefs(c).getString("active_id","");
    SharedPreferences.Editor editor=prefs(c).edit().putString("gallery_json",updated.toString());
    if(id.equals(active))editor.putString("active_id","");
    if(!editor.commit())throw new IOException("删除壁纸记录失败");
    File file=media(c,id);if(file.exists())file.delete();
    File cover=thumbnailFile(c,id);if(cover.exists())cover.delete();
    File qualityMarker=new File(galleryDir(c),id+".hq");if(qualityMarker.exists())qualityMarker.delete();
    return state(c);
  }
  static WebResourceResponse response(Context c,Uri uri,Map<String,String> requestHeaders){
    if(!"asphalt.local".equals(uri.getHost()))return null;
    String path=uri.getPath();if(path==null||(!"/wallpaper/current".equals(path)&&!path.startsWith("/wallpaper/item/")&&!path.startsWith("/wallpaper/thumb/")))return null;
    try {
      if(path.startsWith("/wallpaper/thumb/")){
        String id=path.substring("/wallpaper/thumb/".length());
        File cover="1".equals(uri.getQueryParameter("cached"))?thumbnailFile(c,id):videoThumbnail(c,id);
        if(!cover.isFile()||cover.length()==0)return missing();
        Map<String,String> headers=new HashMap<>();headers.put("Cache-Control","no-store");headers.put("Access-Control-Allow-Origin","*");
        return new WebResourceResponse("image/jpeg",null,200,"OK",headers,new FileInputStream(cover));
      }
      JSONArray all=items(c);String id="/wallpaper/current".equals(path)?prefs(c).getString("active_id",""):path.substring("/wallpaper/item/".length());
      JSONObject selected=entry(all,id);if(selected==null)return missing();
      File file=media(c,id);if(!file.isFile())return missing();
      long length=file.length();String range=null;
      if(requestHeaders!=null)for(Map.Entry<String,String> header:requestHeaders.entrySet())if("Range".equalsIgnoreCase(header.getKey())){range=header.getValue();break;}
      long[] bounds=parseRange(range,length);if(bounds==null)return invalidRange(length);
      long start=bounds[0],end=bounds[1];boolean partial=range!=null;
      FileInputStream in=new FileInputStream(file);try{long left=start;while(left>0){long n=in.skip(left);if(n<=0)throw new IOException("读取壁纸失败");left-=n;}}catch(Exception e){in.close();throw e;}
      Map<String,String> headers=new HashMap<>();headers.put("Accept-Ranges","bytes");headers.put("Content-Length",Long.toString(end-start+1));headers.put("Cache-Control","no-store");headers.put("Access-Control-Allow-Origin","*");headers.put("Access-Control-Expose-Headers","Accept-Ranges, Content-Length, Content-Range");
      if(partial)headers.put("Content-Range","bytes "+start+"-"+end+"/"+length);
      return new WebResourceResponse(selected.optString("mime","image/jpeg"),null,partial?206:200,partial?"Partial Content":"OK",headers,new LimitedStream(in,end-start+1));
    }catch(Exception e){return missing();}
  }
  static long[] parseRange(String header,long length){
    if(length<=0)return null;
    if(header==null)return new long[]{0,length-1};
    if(!header.startsWith("bytes=")||header.indexOf(',')>=0)return null;
    String[] bounds=header.substring(6).split("-",-1);if(bounds.length!=2)return null;
    try{
      if(bounds[0].isEmpty()){
        long suffix=Long.parseLong(bounds[1]);if(suffix<=0)return null;
        return new long[]{Math.max(0,length-suffix),length-1};
      }
      long start=Long.parseLong(bounds[0]);if(start<0||start>=length)return null;
      long end=bounds[1].isEmpty()?length-1:Math.min(length-1,Long.parseLong(bounds[1]));
      return end<start?null:new long[]{start,end};
    }catch(NumberFormatException e){return null;}
  }
  private static WebResourceResponse invalidRange(long length){Map<String,String> headers=new HashMap<>();headers.put("Content-Range","bytes */"+length);headers.put("Access-Control-Allow-Origin","*");return new WebResourceResponse("text/plain","UTF-8",416,"Range Not Satisfiable",headers,new ByteArrayInputStream(new byte[0]));}
  private static WebResourceResponse missing(){return new WebResourceResponse("text/plain","UTF-8",404,"Not Found",null,new ByteArrayInputStream(new byte[0]));}
  private static final class LimitedStream extends FilterInputStream{
    long left;LimitedStream(InputStream in,long count){super(in);left=count;}
    @Override public int read()throws IOException{if(left<=0)return -1;int c=super.read();if(c!=-1)left--;return c;}
    @Override public int read(byte[] buf,int off,int len)throws IOException{if(len==0)return 0;if(left<=0)return -1;int n=super.read(buf,off,(int)Math.min(len,left));if(n>0)left-=n;return n;}
  }
}
