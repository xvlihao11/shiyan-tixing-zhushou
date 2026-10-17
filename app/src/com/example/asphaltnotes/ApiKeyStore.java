package com.example.asphaltnotes.cloudtrial;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class ApiKeyStore {
  private static final String ALIAS="asphalt_ark_key_v1";
  private static final String PREF="cloud_secret";
  private ApiKeyStore(){}
  private static SecretKey key() throws Exception {
    KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);
    SecretKey existing=(SecretKey)store.getKey(ALIAS,null);
    if(existing!=null)return existing;
    KeyGenerator generator=KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore");
    generator.init(new KeyGenParameterSpec.Builder(ALIAS,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT)
      .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
    return generator.generateKey();
  }
  static boolean save(Context context,String value){
    if(value==null||value.trim().isEmpty())return false;
    try{
      Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key());
      byte[] encrypted=cipher.doFinal(value.trim().getBytes(java.nio.charset.StandardCharsets.UTF_8));
      SharedPreferences p=context.getSharedPreferences(PREF,Context.MODE_PRIVATE);
      return p.edit().putString("iv",Base64.encodeToString(cipher.getIV(),Base64.NO_WRAP))
        .putString("data",Base64.encodeToString(encrypted,Base64.NO_WRAP)).commit();
    }catch(Exception e){return false;}
  }
  static String read(Context context){
    try{
      SharedPreferences p=context.getSharedPreferences(PREF,Context.MODE_PRIVATE);
      String iv=p.getString("iv",null),data=p.getString("data",null);
      if(iv==null||data==null)return null;
      Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");
      cipher.init(Cipher.DECRYPT_MODE,key(),new GCMParameterSpec(128,Base64.decode(iv,Base64.NO_WRAP)));
      return new String(cipher.doFinal(Base64.decode(data,Base64.NO_WRAP)),java.nio.charset.StandardCharsets.UTF_8);
    }catch(Exception e){return null;}
  }
  static void clear(Context context){context.getSharedPreferences(PREF,Context.MODE_PRIVATE).edit().clear().apply();}
}
