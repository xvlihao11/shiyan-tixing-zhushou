package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.net.Uri;
import android.util.Base64;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import javax.net.ssl.SSLException;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;

final class CloudRecognizer {
  private static final String ENDPOINT="https://ark.cn-beijing.volces.com/api/v3/responses";
  private static final String MODEL="doubao-seed-2-0-mini-260428";
  private CloudRecognizer(){}

  static JSONObject recognize(Context context,Uri photo,String experiment,String apiKey){
    if(apiKey==null||apiKey.isEmpty())return error("请先在云端识别设置里填写新 API 密钥");
    HttpURLConnection connection=null;
    try{
      String image="data:image/jpeg;base64,"+Base64.encodeToString(imageBytes(context,photo),Base64.NO_WRAP);
      String prompt="你是沥青实验仪器屏幕的原始读数提取器。当前实验类型："+experiment+"。"
        +"只提取已完成的原始测量读数，按照屏幕标签分别记录软化点1/2、软化点A/B、延度或延伸度1/2/3、针入度1/2/3、旋转黏度或旋转粘度1/2/3。若是TRFOT、RTFOT或TFOT老化，分别提取空容器质量、老化前容器加沥青总质量、老化后容器加沥青总质量，并用这三个固定标签输出。电子天平照片若只有一个可信质量数值而无法判定其所属阶段，仍输出该数值，标签写待核对质量，由用户手动对应，不要猜测阶段。"
        +"忽略平均值、温度（包括当前温度和测量温度）、运行时间、时钟、转速、当前长度、拉力、仪器型号、按钮和未完成的读数。"
        +"保持原始小数位，不计算平均值，不修约；模糊或看不清的数字不要猜测。"
        +"只输出合法JSON对象，格式为{\"readings\":[{\"label\":\"软化点A\",\"value\":\"83.78\"}],\"note\":\"\"}。"
        +"value只能是数字字符串；如果没有可信读数就让readings为空数组。不要输出Markdown。";
      JSONObject request=new JSONObject()
        .put("model",MODEL).put("temperature",0.1).put("max_output_tokens",1024)
        .put("input",new JSONArray().put(new JSONObject().put("role","user")
          .put("content",new JSONArray().put(new JSONObject().put("type","input_text").put("text",prompt))
            .put(new JSONObject().put("type","input_image").put("image_url",image)))));
      byte[] body=request.toString().getBytes(StandardCharsets.UTF_8);
      connection=(HttpURLConnection)new URL(ENDPOINT).openConnection();
      connection.setRequestMethod("POST");connection.setConnectTimeout(20000);connection.setReadTimeout(75000);
      connection.setRequestProperty("Authorization","Bearer "+apiKey);
      connection.setRequestProperty("Content-Type","application/json");
      connection.setDoOutput(true);connection.setFixedLengthStreamingMode(body.length);
      try(OutputStream out=connection.getOutputStream()){out.write(body);}
      int code=connection.getResponseCode();
      if(code<200||code>=300){
        String detail="";
        try{
          String raw=readLimited(connection.getErrorStream(),16384);
          JSONObject server=new JSONObject(raw),obj=server.optJSONObject("error");
          if(obj!=null){String errorCode=obj.optString("code","");
            if(errorCode.matches("[A-Za-z0-9_.-]{1,80}"))detail=" · "+errorCode;
          }
        }catch(Exception ignored){}
        return error((code==401?"API 密钥无效或已停用":code==403?"模型或账户无调用权限":"云端拒绝请求（HTTP "+code+"）")+detail);
      }
      JSONObject response=new JSONObject(readLimited(connection.getInputStream(),524288));
      if(response.has("error")&&!response.isNull("error"))return error("云端返回错误，请检查模型和账户设置");
      JSONArray output=response.optJSONArray("output");String answer=null;
      if(output!=null)for(int i=0;i<output.length();i++){
        JSONObject item=output.optJSONObject(i);if(item==null||!"message".equals(item.optString("type")))continue;
        JSONArray content=item.optJSONArray("content");if(content==null)continue;
        for(int j=0;j<content.length();j++){JSONObject part=content.optJSONObject(j);if(part!=null&&"output_text".equals(part.optString("type")))answer=part.optString("text",null);}
      }
      if(answer==null||answer.trim().isEmpty())return error("云端没有返回文本读数，请重拍或稍后再试");
      String clean=answer.trim();if(clean.startsWith("```")){clean=clean.replaceFirst("^```(?:json)?\\s*","").replaceFirst("\\s*```$","").trim();}
      JSONObject parsed=new JSONObject(clean),valid=new JSONObject();JSONArray readings=new JSONArray(),raw=parsed.optJSONArray("readings");
      if(raw!=null)for(int i=0;i<raw.length();i++){
        JSONObject item=raw.optJSONObject(i);if(item==null)continue;
        String label=item.optString("label","").trim(),value=item.optString("value","").trim();
        if(!label.isEmpty()&&value.matches("[+-]?\\d+(?:\\.\\d+)?"))readings.put(new JSONObject().put("label",label).put("value",value));
      }
      valid.put("readings",readings).put("note",parsed.optString("note",""));
      return valid;
    }catch(UnknownHostException e){return error("连接不到火山方舟，请检查手机网络和 DNS");}
    catch(SocketTimeoutException e){return error("云端连接超时，请稍后重试");}
    catch(SSLException e){return error("安全连接失败，请检查手机时间和网络");}
    catch(Exception e){return error("云端识别失败（"+e.getClass().getSimpleName()+"），请检查网络和照片");}
    finally{if(connection!=null)connection.disconnect();}
  }
  private static JSONObject error(String message){try{return new JSONObject().put("error",message);}catch(Exception ignored){return new JSONObject();}}
  private static String readLimited(InputStream stream,int max) throws Exception{
    if(stream==null)return "";
    try(InputStream in=stream;ByteArrayOutputStream out=new ByteArrayOutputStream()){
      byte[] buf=new byte[8192];int n;while((n=in.read(buf))!=-1){if(out.size()+n>max)throw new Exception("响应过大");out.write(buf,0,n);}
      return out.toString("UTF-8");
    }
  }
  private static byte[] imageBytes(Context context,Uri photo) throws Exception{
    // MainActivity supplies PhotoStore's verified compact JPEG. Upload those
    // exact bytes, without another decode/re-encode or a second stored copy.
    ByteArrayOutputStream out=new ByteArrayOutputStream();
    try(InputStream in=context.getContentResolver().openInputStream(photo)){
      if(in==null)throw new Exception("无法读取照片附件");
      byte[] buffer=new byte[16384];int count;
      while((count=in.read(buffer))!=-1){
        if(out.size()+count>8*1024*1024)throw new Exception("照片附件过大，请重新添加照片");
        out.write(buffer,0,count);
      }
    }
    if(out.size()==0)throw new Exception("照片附件为空");
    return out.toByteArray();
  }
}
