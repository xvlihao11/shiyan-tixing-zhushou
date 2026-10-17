package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.net.Uri;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.webkit.WebResourceResponse;
import java.io.*;
import java.util.*;

/** Gallery originals are independent. Each attachment keeps one compact JPEG. */
final class PhotoStore {
  static final int MAX_EDGE=1920, TARGET_BYTES=500*1024;
  static File file(Context c,String id,String variant) {
    if(id==null||!id.matches("[a-f0-9-]{36}")||!(variant.equals("compact")||variant.equals("original")||variant.equals("preview")||variant.equals("thumb")))throw new IllegalArgumentException("无效照片编号");
    return new File(new File(c.getFilesDir(),"experiment_photos"),id+"_"+variant+".jpg");
  }
  static synchronized String save(Context c,Uri source) throws Exception {
    String id=UUID.randomUUID().toString();
    writeCompact(c,id,source);
    return id;
  }
  private static void writeCompact(Context c,String id,Uri source) throws Exception {
    BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;
    try(InputStream in=c.getContentResolver().openInputStream(source)){
      if(in==null)throw new IOException("照片不可读取");
      BitmapFactory.decodeStream(in,null,bounds);
    }
    if(bounds.outWidth<=0||bounds.outHeight<=0)throw new IOException("不是有效图片");
    BitmapFactory.Options opts=new BitmapFactory.Options();opts.inSampleSize=1;
    // Downsample during decode, then scale once to the exact upper bound.
    while(Math.max(bounds.outWidth,bounds.outHeight)/opts.inSampleSize>2560)opts.inSampleSize*=2;
    Bitmap image;
    try(InputStream in=c.getContentResolver().openInputStream(source)){image=BitmapFactory.decodeStream(in,null,opts);}
    if(image==null)throw new IOException("无法读取照片");
    File target=file(c,id,"compact"),temporary=new File(target.toString()+".tmp");
    try {
      float ratio=Math.min(1f,(float)MAX_EDGE/Math.max(image.getWidth(),image.getHeight()));
      if(ratio<1f){
        Bitmap resized=Bitmap.createScaledBitmap(image,Math.max(1,Math.round(image.getWidth()*ratio)),Math.max(1,Math.round(image.getHeight()*ratio)),true);
        if(resized!=image)image.recycle();image=resized;
      }
      int orientation=1;
      try(InputStream in=c.getContentResolver().openInputStream(source)){orientation=new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION,1);}
      catch(IOException ignored){}
      Matrix m=new Matrix();
      switch(orientation){case 2:m.setScale(-1,1);break;case 3:m.setRotate(180);break;case 4:m.setScale(1,-1);break;case 5:m.setRotate(90);m.postScale(-1,1);break;case 6:m.setRotate(90);break;case 7:m.setRotate(-90);m.postScale(-1,1);break;case 8:m.setRotate(-90);break;}
      if(!m.isIdentity()){Bitmap rotated=Bitmap.createBitmap(image,0,0,image.getWidth(),image.getHeight(),m,true);if(rotated!=image)image.recycle();image=rotated;}
      ByteArrayOutputStream bytes=new ByteArrayOutputStream();
      if(!image.compress(Bitmap.CompressFormat.JPEG,85,bytes))throw new IOException("照片压缩失败");
      if(bytes.size()>TARGET_BYTES){
        bytes.reset();
        if(!image.compress(Bitmap.CompressFormat.JPEG,75,bytes))throw new IOException("照片压缩失败");
      }
      // Size is a target, not a hard cap: preserve text detail on dense images.
      target.getParentFile().mkdirs();
      try(FileOutputStream out=new FileOutputStream(temporary)){bytes.writeTo(out);out.getFD().sync();}
      if(!valid(temporary))throw new IOException("压缩照片校验失败");
      if(!temporary.renameTo(target))throw new IOException("照片保存失败");
    }finally{image.recycle();temporary.delete();}
  }
  private static boolean valid(File f){
    if(!f.isFile()||f.length()==0)return false;
    BitmapFactory.Options opts=new BitmapFactory.Options();opts.inSampleSize=8;
    Bitmap b=BitmapFactory.decodeFile(f.toString(),opts);
    if(b==null)return false;b.recycle();return true;
  }
  static synchronized File ensureCompact(Context c,String id) throws Exception {
    File compact=file(c,id,"compact");
    if(!valid(compact)){
      File source=file(c,id,"original");
      if(!source.isFile())source=file(c,id,"preview");
      if(!source.isFile())source=file(c,id,"thumb");
      if(!source.isFile())throw new IOException("照片附件不在本机，请重新添加照片");
      writeCompact(c,id,Uri.fromFile(source));
    }
    // Only private legacy copies are removed, after a verified atomic commit.
    for(String variant:new String[]{"original","preview","thumb"})file(c,id,variant).delete();
    return compact;
  }
  static synchronized void rotate(Context c,String id) throws Exception {
    File target=ensureCompact(c,id),temp=new File(target.toString()+".rotate.tmp");
    Bitmap image=BitmapFactory.decodeFile(target.toString());
    if(image==null)throw new IOException("照片无法读取");
    Bitmap rotated=null;
    try {
      Matrix matrix=new Matrix();matrix.setRotate(90);
      rotated=Bitmap.createBitmap(image,0,0,image.getWidth(),image.getHeight(),matrix,true);
      try(FileOutputStream out=new FileOutputStream(temp)){
        if(!rotated.compress(Bitmap.CompressFormat.JPEG,90,out))throw new IOException("旋转保存失败");
        out.getFD().sync();
      }
      if(!valid(temp)||!temp.renameTo(target))throw new IOException("旋转照片保存失败");
    }finally{if(rotated!=null&&rotated!=image)rotated.recycle();image.recycle();temp.delete();}
  }
  static synchronized void delete(Context c,String id) throws Exception {
    for(String variant:new String[]{"compact","original","preview","thumb"}){
      File f=file(c,id,variant);
      if(f.exists()&&!f.delete())throw new IOException("照片删除失败，请重试");
    }
  }
  static List<String> legacyIds(Context c){
    File dir=new File(c.getFilesDir(),"experiment_photos");
    String[] names=dir.list();Set<String> ids=new LinkedHashSet<>();
    if(names!=null)for(String name:names){
      if(name.matches("[a-f0-9-]{36}_(original|preview|thumb)\\.jpg"))ids.add(name.substring(0,36));
    }
    return new ArrayList<>(ids);
  }
  static WebResourceResponse response(Context c,Uri uri){
    if(!"asphalt.local".equals(uri.getHost()))return null;
    try {
      List<String> parts=uri.getPathSegments();
      if(parts.size()!=3||!parts.get(0).equals("photos")||!(parts.get(2).equals("preview")||parts.get(2).equals("thumb")))throw new IOException();
      File image=file(c,parts.get(1),"compact");
      // Existing URLs keep working before and after background conversion.
      if(!image.isFile())image=file(c,parts.get(1),parts.get(2));
      return new WebResourceResponse("image/jpeg",null,new FileInputStream(image));
    }catch(Exception e){return new WebResourceResponse("text/plain","UTF-8",404,"Not Found",null,new ByteArrayInputStream(new byte[0]));}
  }
}
