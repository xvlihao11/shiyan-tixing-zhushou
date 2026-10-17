package com.example.asphaltnotes.cloudtrial;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Imports the selected PKGM asset directly, including non-seekable providers. */
final class MpkgStream {
  private static final long PACKAGE_CAP=130L*1024*1024,ASSET_CAP=120L*1024*1024;
  static MpkgArchive.Choice extract(InputStream source,long sourceLength,File target)throws IOException{
    try(InputStream in=new BufferedInputStream(source,262144)){
      long total=sourceLength>=0?sourceLength:PACKAGE_CAP;
      if(total<20||total>PACKAGE_CAP)throw new IOException("MPKG 文件不完整或超过 130 MB");
      if(readInt(in)!=8)throw new IOException("不是支持的 Wallpaper Engine MPKG");
      byte[] magic=new byte[8];readFully(in,magic);
      if(!new String(magic,StandardCharsets.US_ASCII).matches("PKGM[0-9]{4}"))throw new IOException("不是支持的 Wallpaper Engine MPKG");
      int count=readInt(in);if(count<1||count>10000)throw new IOException("MPKG 目录无效");
      List<MpkgArchive.Choice> files=new ArrayList<>();long dataStart=16;
      for(int i=0;i<count;i++){
        int size=readInt(in);if(size<1||size>4096||dataStart+size+12>total)throw new IOException("MPKG 文件名无效");
        byte[] bytes=new byte[size];readFully(in,bytes);
        String name=new String(bytes,0,bytes[size-1]==0?size-1:size,StandardCharsets.UTF_8);
        long offset=Integer.toUnsignedLong(readInt(in)),length=Integer.toUnsignedLong(readInt(in));
        if(length>ASSET_CAP)throw new IOException("MPKG 中的素材超过 120 MB");
        String lower=name.toLowerCase(Locale.ROOT);
        String kind=lower.matches(".*\\.(mp4|m4v|mov|3gp|3g2|webm)")?"video":lower.matches(".*\\.(jpg|jpeg|png|webp|gif)")?"image":"";
        files.add(new MpkgArchive.Choice(name,kind,offset,length));dataStart+=size+12;
      }
      MpkgArchive.Choice video=null,preview=null,image=null;
      for(MpkgArchive.Choice file:files){
        if(file.offset>total-dataStart||file.length>total-dataStart-file.offset)throw new IOException("MPKG 素材超出文件范围");
        if(file.kind.equals("video")&&(video==null||file.length>video.length))video=file;
        if(file.kind.equals("image")){
          if(file.name.toLowerCase(Locale.ROOT).matches("(^|.*/)preview\\.(jpg|jpeg|png|webp|gif)"))preview=file;
          if(image==null)image=file;
        }
      }
      MpkgArchive.Choice picked=video!=null?video:preview!=null?preview:image;
      if(picked==null)throw new IOException("此 MPKG 没有可提取的视频或预览图");
      byte[] buffer=new byte[262144];long skip=picked.offset;
      while(skip>0){
        long n=in.skip(skip);
        if(n<=0){int read=in.read(buffer,0,(int)Math.min(skip,buffer.length));if(read<0)throw new EOFException("MPKG 素材不完整");if(read==0){if(in.read()<0)throw new EOFException("MPKG 素材不完整");read=1;}n=read;}
        skip-=n;
      }
      try(FileOutputStream out=new FileOutputStream(target)){
        long left=picked.length;
        while(left>0){int n=in.read(buffer,0,(int)Math.min(left,buffer.length));if(n<0)throw new EOFException("MPKG 素材不完整");if(n==0){int value=in.read();if(value<0)throw new EOFException("MPKG 素材不完整");out.write(value);left--;}else{out.write(buffer,0,n);left-=n;}}
        out.getFD().sync();
      }
      return new MpkgArchive.Choice(picked.name,picked.kind,dataStart+picked.offset,picked.length);
    }catch(IOException e){target.delete();throw e;}
  }
  private static void readFully(InputStream in,byte[] bytes)throws IOException{
    int pos=0;while(pos<bytes.length){int n=in.read(bytes,pos,bytes.length-pos);if(n<0)throw new EOFException("MPKG 目录不完整");if(n==0){int value=in.read();if(value<0)throw new EOFException("MPKG 目录不完整");bytes[pos++]=(byte)value;}else pos+=n;}
  }
  private static int readInt(InputStream in)throws IOException{
    int a=in.read(),b=in.read(),c=in.read(),d=in.read();if((a|b|c|d)<0)throw new EOFException("MPKG 目录不完整");return a|(b<<8)|(c<<16)|(d<<24);
  }
}
