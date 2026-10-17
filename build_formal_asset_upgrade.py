#!/usr/bin/env python3
"""V5.47 safe asset-only formal upgrade from *existing* V5.46 formal APK.

Unlike the historical rebuild, this script retains every existing native DEX and
resource, replaces only the audited wallpaper/front-end files and requires the
ORIGINAL release private key before writing an installable APK. It stops rather
than making an incompatible signature. Intended for signing where local.keystore
is available; do not distribute the intermediate unsigned file as installable.
"""
from pathlib import Path
from zipfile import ZipFile, ZipInfo
from cryptography.hazmat.primitives.serialization import pkcs12, Encoding, PublicFormat
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import hashes
import argparse, io, os, hashlib, struct

ROOT=Path(__file__).resolve().parent

def lp(value): return struct.pack('<I',len(value))+value

def apk_certificate(raw):
    eocd=raw.rfind(b'PK\x05\x06');cd=struct.unpack_from('<I',raw,eocd+16)[0]
    assert raw[cd-16:cd]==b'APK Sig Block 42'
    size=struct.unpack_from('<Q',raw,cd-24)[0];start=cd-size-8
    pair_size=struct.unpack_from('<Q',raw,start+8)[0]
    assert struct.unpack_from('<I',raw,start+16)[0]==0x7109871a
    val=raw[start+20:start+16+pair_size]
    def read(v,p):
        n=struct.unpack_from('<I',v,p)[0];return v[p+4:p+4+n],p+4+n
    seq,_=read(val,0);signer,_=read(seq,0);signed,_=read(signer,0)
    _,p=read(signed,0);certs,_=read(signed,p);cert,_=read(certs,0)
    return cert

def manifest_patch(xml):
    b=bytearray(xml);pool=8;assert struct.unpack_from('<H',b,0)[0]==3
    count=struct.unpack_from('<I',b,pool+8)[0]
    start=pool+struct.unpack_from('<I',b,pool+20)[0]
    head=struct.unpack_from('<H',b,pool+2)[0]
    values=[];refs={}
    for i in range(count):
        p=start+struct.unpack_from('<I',b,pool+head+i*4)[0];n=struct.unpack_from('<H',b,p)[0]
        v=b[p+2:p+2+2*n].decode('utf-16le');values.append(v);refs.setdefault(v,[]).append((p,n))
    assert 'com.example.asphaltnotes' in refs
    for old,new in [('5.46','5.47'),('沥青实验记录5.46','沥青实验记录5.47')]:
        assert old in refs,(old,'not in binary manifest')
        for p,n in refs[old]:
            assert len(old)==len(new)==n
            b[p+2:p+2+2*n]=new.encode('utf-16le')
    pos=pool+struct.unpack_from('<I',b,pool+4)[0];changed=False
    while pos<len(b):
        typ,h,size=struct.unpack_from('<HHI',b,pos)
        assert size>=h>0 and pos+size<=len(b)
        if typ==0x0102 and values[struct.unpack_from('<I',b,pos+20)[0]]=='manifest':
            attr_start,attr_size,num=struct.unpack_from('<HHH',b,pos+24)
            for i in range(num):
                a=pos+16+attr_start+i*attr_size
                if values[struct.unpack_from('<I',b,a+4)[0]]=='versionCode':
                    assert b[a+15]==0x10
                    assert struct.unpack_from('<I',b,a+16)[0]==80
                    struct.pack_into('<I',b,a+16,81);changed=True
            break
        pos+=size
    assert changed,'version code not changed'
    return bytes(b)

def unsigned_package(base):
    out=io.BytesIO()
    changed={'assets/wallpaper.js','assets/ui_polish.css','assets/index.html'}
    with ZipFile(base) as src, ZipFile(out,'w') as dst:
        assert src.testzip() is None
        assert changed.issubset(set(src.namelist()))
        for item in src.infolist():
            name=item.filename;data=src.read(name)
            if name in changed:
                data=(ROOT/'app/assets'/name[7:]).read_bytes()
                if name=='assets/index.html':
                    data=data.replace('V5.46'.encode(),'V5.47'.encode())
                    assert b'V5.47' in data
            elif name=='AndroidManifest.xml':data=manifest_patch(data)
            if name=='resources.arsc':
                item.extra=b''
                pad=(-out.tell()-30-len(name.encode('utf-8'))-4)%4
                item.extra=struct.pack('<HH',0xcafe,pad)+bytes(pad)
                assert item.compress_type==0
            dst.writestr(item,data)
    return out.getvalue()

def digest(parts):
    chunks=[hashlib.sha256(b'\xa5'+struct.pack('<I',len(c))+c).digest()
            for part in parts for c in (part[i:i+1048576] for i in range(0,len(part),1048576))]
    return hashlib.sha256(b'\x5a'+struct.pack('<I',len(chunks))+b''.join(chunks)).digest()

def sign_apk(raw,key,cert):
    eocd=raw.rfind(b'PK\x05\x06');cd=struct.unpack_from('<I',raw,eocd+16)[0]
    assert raw[cd:cd+4]==b'PK\x01\x02'
    algorithm=0x0103
    digest_record=lp(struct.pack('<I',algorithm)+lp(digest((raw[:cd],raw[cd:eocd],raw[eocd:]))))
    signed=lp(digest_record)+lp(lp(cert.public_bytes(Encoding.DER)))+lp(b'')
    signature=key.sign(signed,padding.PKCS1v15(),hashes.SHA256())
    signer=lp(signed)+lp(lp(struct.pack('<I',algorithm)+lp(signature)))+lp(key.public_key().public_bytes(Encoding.DER,PublicFormat.SubjectPublicKeyInfo))
    signers=lp(signer);pair=struct.pack('<Q',len(lp(signers))+4)+struct.pack('<I',0x7109871a)+lp(signers)
    size=len(pair)+24;block=struct.pack('<Q',size)+pair+struct.pack('<Q',size)+b'APK Sig Block 42'
    end=bytearray(raw[eocd:]);struct.pack_into('<I',end,16,cd+len(block))
    return raw[:cd]+block+raw[cd:eocd]+end

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--previous-apk',type=Path,required=True,help='Original formal V5.46 signed APK')
    parser.add_argument('--keystore',type=Path,required=True,help='Original local.keystore, not a new key')
    parser.add_argument('--out',type=Path,default=ROOT/'release/沥青实验记录_V5.47_正式签名升级版.apk')
    args=parser.parse_args()
    for f in (args.previous_apk,args.keystore):
        if not f.is_file():parser.error('Missing '+str(f))
    keypassword=os.environ.get('ASPHALT_RELEASE_KEY_PASSWORD')
    if not keypassword:parser.error('Set ASPHALT_RELEASE_KEY_PASSWORD in the environment')
    key,cert,_=pkcs12.load_key_and_certificates(args.keystore.read_bytes(),keypassword.encode())
    if key is None or cert is None:parser.error('Original key and certificate missing')
    old=args.previous_apk.read_bytes()
    if cert.public_bytes(Encoding.DER)!=apk_certificate(old):parser.error('Keystore does not match the original APK signing certificate; refusing incompatible upgrade')
    unsigned=unsigned_package(args.previous_apk)
    signed=sign_apk(unsigned,key,cert)
    from verify_release import signature, manifest_fields
    if signature(signed)!=signature(old):raise ValueError('Verification failed: signing identity changed')
    with ZipFile(io.BytesIO(signed)) as dst,ZipFile(args.previous_apk) as src:
        if dst.testzip() is not None:raise ValueError('APK CRC invalid')
        strings,code=manifest_fields(dst.read('AndroidManifest.xml'))
        assert code==81 and '5.47' in strings and 'com.example.asphaltnotes' in strings
        for n in ('classes.dex','classes2.dex','resources.arsc'):
            assert dst.read(n)==src.read(n),'Unexpected native/resource mutation'
        for n in ('wallpaper.js','ui_polish.css','index.html'):
            expected=(ROOT/'app/assets'/n).read_bytes()
            if n=='index.html':expected=expected.replace(b'V5.46',b'V5.47')
            assert dst.read('assets/'+n)==expected
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_bytes(signed)
    print('Verified original signing identity, version 81, and unchanged native app:',args.out)

if __name__=='__main__':main()
