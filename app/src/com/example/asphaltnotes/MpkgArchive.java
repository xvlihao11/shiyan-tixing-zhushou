package com.example.asphaltnotes.cloudtrial;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Bounded reader for Wallpaper Engine PKGM packages. Extracts one displayable asset only. */
final class MpkgArchive {
  static final class Choice {
    final String name,kind;
    final long offset,length;
    Choice(String name,String kind,long offset,long length){this.name=name;this.kind=kind;this.offset=offset;this.length=length;}
  }
  static Choice choose(File packageFile) throws IOException {return choose(packageFile,false);}
  static Choice preview(File packageFile) throws IOException {return choose(packageFile,true);}
  private static Choice choose(File packageFile,boolean preferPreview) throws IOException {
    try(RandomAccessFile in=new RandomAccessFile(packageFile,"r")){
      long total=in.length();
      if(total<20)throw new IOException("MPKG 文件不完整");
      int magicLength=readInt(in);
      if(magicLength!=8)throw new IOException("不是支持的 Wallpaper Engine MPKG");
      byte[] magic=new byte[8];in.readFully(magic);
      String format=new String(magic,StandardCharsets.US_ASCII);
      if(!format.matches("PKGM[0-9]{4}"))throw new IOException("不是支持的 Wallpaper Engine MPKG");
      int count=readInt(in);
      if(count<1||count>10000)throw new IOException("MPKG 目录无效");
      List<Choice> files=new ArrayList<>();
      for(int i=0;i<count;i++){
        int nameLength=readInt(in);
        if(nameLength<1||nameLength>4096||in.getFilePointer()+nameLength+8>total)throw new IOException("MPKG 文件名无效");
        byte[] bytes=new byte[nameLength];in.readFully(bytes);
        String name=new String(bytes,0,bytes[nameLength-1]==0?nameLength-1:nameLength,StandardCharsets.UTF_8);
        long offset=Integer.toUnsignedLong(readInt(in)),length=Integer.toUnsignedLong(readInt(in));
        if(length>120L*1024*1024)throw new IOException("MPKG 中的素材超过 120 MB");
        String lower=name.toLowerCase(Locale.ROOT);
        String kind=lower.matches(".*\\.(mp4|m4v|mov|3gp|3g2|webm)")?"video":
          lower.matches(".*\\.(jpg|jpeg|png|webp|gif)")?"image":"";
        files.add(new Choice(name,kind,offset,length));
      }
      long dataStart=in.getFilePointer();
      for(Choice file:files)if(file.offset>total-dataStart||file.length>total-dataStart-file.offset)
        throw new IOException("MPKG 素材超出文件范围");
      Choice video=null,preview=null,image=null;
      for(Choice file:files){
        String lower=file.name.toLowerCase(Locale.ROOT);
        if(file.kind.equals("video")&&(video==null||file.length>video.length))video=file;
        if(file.kind.equals("image")){
          if(lower.matches("(^|.*/)preview\\.(jpg|jpeg|png|webp|gif)"))preview=file;
          if(image==null)image=file;
        }
      }
      Choice picked=preferPreview?(preview!=null?preview:image):(video!=null?video:preview!=null?preview:image);
      if(picked==null){if(preferPreview)return null;throw new IOException("此 MPKG 没有可提取的视频或预览图");}
      return new Choice(picked.name,picked.kind,dataStart+picked.offset,picked.length);
    }
  }
  static void extract(File source,Choice choice,File target) throws IOException {
    try(RandomAccessFile in=new RandomAccessFile(source,"r");FileOutputStream out=new FileOutputStream(target)){
      in.seek(choice.offset);
      long left=choice.length;byte[] buffer=new byte[32768];
      while(left>0){int n=in.read(buffer,0,(int)Math.min(left,buffer.length));if(n<0)throw new EOFException("MPKG 素材不完整");out.write(buffer,0,n);left-=n;}
      out.getFD().sync();
    }
  }
  private static int readInt(RandomAccessFile in) throws IOException {
    int a=in.read(),b=in.read(),c=in.read(),d=in.read();
    if((a|b|c|d)<0)throw new EOFException("MPKG 目录不完整");
    return a|(b<<8)|(c<<16)|(d<<24);
  }
}
