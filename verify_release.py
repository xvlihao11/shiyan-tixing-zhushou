from pathlib import Path
from zipfile import ZipFile
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import hashes
import argparse,hashlib,struct,zlib

ROOT=Path(__file__).resolve().parent
APK=ROOT/'release/沥青实验记录_V5.46_壁纸滑动与加载修复正式版.apk'
OLD=Path('/workspace/scratch/18f4bdc5f5d1/formal_v545_work/release/沥青实验记录_V5.45_高清壁纸与厚玻璃正式版.apk')
TRIAL=Path('/workspace/scratch/18f4bdc5f5d1/cloud_trial/release/沥青实验记录_云识别_对齐修复版.apk')

def lp(value,pos):
    length=struct.unpack_from('<I',value,pos)[0]
    return value[pos+4:pos+4+length],pos+4+length

def signature(apk):
    eocd=apk.rfind(b'PK\x05\x06');cd=struct.unpack_from('<I',apk,eocd+16)[0]
    size=struct.unpack_from('<Q',apk,cd-24)[0];start=cd-size-8
    assert apk[cd-16:cd]==b'APK Sig Block 42'
    assert struct.unpack_from('<Q',apk,start)[0]==size
    pair_size=struct.unpack_from('<Q',apk,start+8)[0]
    assert struct.unpack_from('<I',apk,start+16)[0]==0x7109871a
    seq,pos=lp(apk[start+20:start+16+pair_size],0)
    signer,pos=lp(seq,0)
    signed,pos=lp(signer,0);sigs,pos=lp(signer,pos);public,pos=lp(signer,pos)
    digests,pos=lp(signed,0);certs,pos=lp(signed,pos)
    digest_record,_=lp(digests,0);algorithm=struct.unpack_from('<I',digest_record,0)[0]
    digest_expected,_=lp(digest_record,4)
    sig_record,_=lp(sigs,0)
    assert struct.unpack_from('<I',sig_record,0)[0]==algorithm==0x0103
    sig,_=lp(sig_record,4);certder,_=lp(certs,0)
    cert=x509.load_der_x509_certificate(certder)
    cert.public_key().verify(sig,signed,padding.PKCS1v15(),hashes.SHA256())
    end=bytearray(apk[eocd:]);struct.pack_into('<I',end,16,start)
    chunks=[hashlib.sha256(b'\xa5'+struct.pack('<I',len(c))+c).digest()
            for part in (apk[:start],apk[cd:eocd],end)
            for c in (part[i:i+1048576] for i in range(0,len(part),1048576))]
    actual=hashlib.sha256(b'\x5a'+struct.pack('<I',len(chunks))+b''.join(chunks)).digest()
    assert digest_expected==actual
    return cert.fingerprint(hashes.SHA256())

def manifest_fields(binary):
    pool=8;count=struct.unpack_from('<I',binary,pool+8)[0]
    base=pool+struct.unpack_from('<I',binary,pool+20)[0]
    header=struct.unpack_from('<H',binary,pool+2)[0]
    strings=[]
    for i in range(count):
        p=base+struct.unpack_from('<I',binary,pool+header+4*i)[0]
        n=struct.unpack_from('<H',binary,p)[0]
        strings.append(binary[p+2:p+2+n*2].decode('utf-16le'))
    code=None;pos=pool+struct.unpack_from('<I',binary,pool+4)[0]
    while pos<len(binary):
        typ,head,size=struct.unpack_from('<HHI',binary,pos)
        if typ==0x0102 and strings[struct.unpack_from('<I',binary,pos+20)[0]]=='manifest':
            start,stride,count=struct.unpack_from('<HHH',binary,pos+24)
            for i in range(count):
                p=pos+16+start+i*stride
                if strings[struct.unpack_from('<I',binary,p+4)[0]]=='versionCode':
                    code=struct.unpack_from('<I',binary,p+16)[0]
            break
        pos+=size
    return strings,code

def main():
    global APK,OLD,TRIAL
    parser=argparse.ArgumentParser(description='Verify formal V5.46 signature, version and resources')
    parser.add_argument('--apk',type=Path,default=APK)
    parser.add_argument('--previous-apk',type=Path,default=OLD)
    parser.add_argument('--base-apk',type=Path,default=TRIAL)
    args=parser.parse_args();APK,OLD,TRIAL=args.apk,args.previous_apk,args.base_apk
    signed=APK.read_bytes()
    assert signature(signed)==signature(OLD.read_bytes()),'not the formal signing identity'
    with ZipFile(OLD) as previous:
        old_strings,old_version=manifest_fields(previous.read('AndroidManifest.xml'))
        assert old_version==79 and 'com.example.asphaltnotes' in old_strings
        assert b"const KEY='asphalt_lab_v1'" in previous.read('assets/index.html')
    with ZipFile(APK) as z,ZipFile(TRIAL) as trial:
        assert z.testzip() is None
        info=z.getinfo('resources.arsc');name,extra=struct.unpack_from('<HH',signed,info.header_offset+26)
        assert info.compress_type==0 and (info.header_offset+30+name+extra)%4==0
        assert 'com.example.asphaltnotes'.encode('utf-16le') in z.read('resources.arsc')
        strings,version=manifest_fields(z.read('AndroidManifest.xml'))
        assert version==80 and '5.46' in strings
        assert 'com.example.asphaltnotes' in strings
        assert 'com.example.asphaltnotes.cloudtrial.MainActivity' in strings
        assert 'android.permission.INTERNET' in strings
        assert '沥青实验记录5.46' in strings
        for name in ('classes.dex','classes2.dex'):
            d=z.read(name)
            assert d==((ROOT/'native_build/dex/classes.dex').read_bytes() if name=='classes2.dex' else trial.read(name))
            assert d[12:32]==hashlib.sha1(d[32:]).digest()
            assert struct.unpack_from('<I',d,8)[0]==zlib.adler32(d[12:])&0xffffffff
        assert b'dispatchKeyEvent' in z.read('classes2.dex')
        assert b'activateWallpaper' in z.read('classes2.dex')
        assert b'deleteWallpaper' in z.read('classes2.dex')
        assert b'renameWallpaper' in z.read('classes2.dex')
        assert b'/wallpaper/thumb/' in z.read('classes2.dex')
        assert b'ui_wallpaper_gallery' in z.read('classes2.dex')
        assert 'assets/documents.js' in z.namelist()
        assert 'assets/pdf.min.js' in z.namelist()
        assert 'assets/export_multi.js' in z.namelist()
        assert 'assets/cloud.js' in z.namelist()
        assert 'assets/yield.js' in z.namelist()
        for name in ('sample_tools.js','sample_tools.css','internal_standards.js','internal_standards.css','backup_full.js','text_export.js','wallpaper.js','ui_polish.css'):
            assert z.read('assets/'+name)==(ROOT/'app/assets'/name).read_bytes()
        assert b'backupStart' in z.read('classes2.dex')
        assert b'backupCommitPhotos' in z.read('classes2.dex')
        assert b'textExportStart' in z.read('classes2.dex')
        assert b'pending_text_export.bin' in z.read('classes2.dex')
        assert b'pending_full_backup.zip' in z.read('classes2.dex')
        assert b'backupFinishLocal' in z.read('classes2.dex')
        assert b'textExportFinishLocal' in z.read('classes2.dex')
        assert b"const KEY='asphalt_lab_v1'" in z.read('assets/index.html')
        assert b'cloudtrial' not in z.read('assets/index.html')
        assert not any('mlkit' in n.lower() for n in z.namelist())
    print('Formal V5.46: original package, formal signer, version 80, cloud code, export, resource alignment and APK v2 digest verified')

if __name__=='__main__':main()
