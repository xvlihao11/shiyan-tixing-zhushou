package com.example.asphaltnotes.cloudtrial;
import android.content.Context;
import android.net.Uri;
import java.io.*;
import java.security.MessageDigest;

/** Do not report backup success until destination bytes have been read back. */
final class VerifiedFileWriter {
  static long copy(Context context,File source,Uri destination)throws Exception {
    long expected=source.length();if(!source.isFile()||expected<=0)throw new IOException("待导出文件为空，未保存，请重新导出");
    MessageDigest sourceHash=MessageDigest.getInstance("SHA-256");long written=0;
    try(InputStream in=new FileInputStream(source);OutputStream out=context.getContentResolver().openOutputStream(destination,"w")){
      if(out==null)throw new IOException("所选位置无法写入");byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1){out.write(b,0,n);sourceHash.update(b,0,n);written+=n;}out.flush();
    }
    if(written!=expected)throw new IOException("导出字节不完整，请重新导出");
    MessageDigest targetHash=MessageDigest.getInstance("SHA-256");long actual=0;
    try(InputStream in=context.getContentResolver().openInputStream(destination)){
      if(in==null)throw new IOException("不能读取保存的文件");byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1){targetHash.update(b,0,n);actual+=n;}
    }catch(Exception e){throw new IOException("文件已写入但无法回读校验，请改存手机本机的下载目录："+e.getMessage(),e);}
    if(actual==0)throw new IOException("保存后文件为 0 B，备份失败；请选择手机本机的下载目录重新导出");
    if(actual!=expected||!java.util.Arrays.equals(sourceHash.digest(),targetHash.digest()))throw new IOException("保存后的文件校验失败，请重新导出");
    return actual;
  }
}
