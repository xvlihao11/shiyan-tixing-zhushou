"""Rebuild native DEX and package an original formal application identity.
The original signed APK supplies compiled resources; fresh D8 output supplies native code.
"""
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.serialization import pkcs12
import argparse, hashlib, io, struct

ROOT=Path(__file__).resolve().parent
SOURCE=Path('/workspace/scratch/18f4bdc5f5d1/cloud_trial/release/沥青实验记录_云识别_对齐修复版.apk')
FORMAL=Path('/workspace/scratch/18f4bdc5f5d1/formal_v545_work/release/沥青实验记录_V5.45_高清壁纸与厚玻璃正式版.apk')
KEY=Path('/workspace/scratch/e72a4c5082ed/asphalt_app/build/local.keystore')
OUT=ROOT/'release/沥青实验记录_V5.46_壁纸滑动与加载修复正式版.apk'


def modify_manifest(source):
    data=bytearray(source)
    assert struct.unpack_from('<H',data,0)[0]==3
    pool=8
    assert struct.unpack_from('<H',data,pool)[0]==1
    count=struct.unpack_from('<I',data,pool+8)[0]
    assert not struct.unpack_from('<I',data,pool+16)[0]&0x100  # UTF-16 strings
    strings_start=struct.unpack_from('<I',data,pool+20)[0]
    header=struct.unpack_from('<H',data,pool+2)[0]
    offsets=[];strings=[]
    for i in range(count):
        pos=pool+strings_start+struct.unpack_from('<I',data,pool+header+4*i)[0]
        length=struct.unpack_from('<H',data,pos)[0]
        text=data[pos+2:pos+2+2*length].decode('utf-16le')
        offsets.append((pos,length));strings.append(text)
    changes={
        'com.example.asphaltnotes.cloudtrial':'com.example.asphaltnotes',
        '5.6 云识别测试':'5.46',
        '沥青记录 云识别测试':'沥青实验记录5.46',
    }
    for old,new in changes.items():
        assert strings.count(old)==1,old
        idx=strings.index(old);pos,length=offsets[idx]
        assert len(new)<=length
        struct.pack_into('<H',data,pos,len(new))
        payload=new.encode('utf-16le')+b'\0\0'
        data[pos+2:pos+2+2*length+2]=payload+b'\0'*(2*(length-len(new)))
    # Root manifest start tag: typed integer attribute versionCode.
    pos=pool+struct.unpack_from('<I',data,pool+4)[0]
    updated=False
    while pos<len(data):
        typ,header,size=struct.unpack_from('<HHI',data,pos)
        assert size>=header and pos+size<=len(data)
        if typ==0x0102 and strings[struct.unpack_from('<I',data,pos+20)[0]]=='manifest':
            attr_start,attr_size,attr_count=struct.unpack_from('<HHH',data,pos+24)
            for i in range(attr_count):
                p=pos+16+attr_start+i*attr_size
                if strings[struct.unpack_from('<I',data,p+4)[0]]=='versionCode':
                    assert data[p+15]==0x10 and struct.unpack_from('<I',data,p+16)[0]==1
                    struct.pack_into('<I',data,p+16,80);updated=True
            break
        pos+=size
    assert updated
    return bytes(data)


def modify_resources(source):
    data=bytearray(source)
    assert struct.unpack_from('<H',data,0)[0]==0x0002
    pos=struct.unpack_from('<H',data,2)[0]
    while pos<len(data):
        typ,header,size=struct.unpack_from('<HHI',data,pos)
        assert size>=header and pos+size<=len(data)
        if typ==0x0200:
            old='com.example.asphaltnotes.cloudtrial'.encode('utf-16le')
            start=pos+12
            assert data[start:start+len(old)]==old
            name='com.example.asphaltnotes'.encode('utf-16le')
            data[start:start+256]=name+bytes(256-len(name))
            return bytes(data)
        pos+=size
    raise ValueError('missing resource package')


def lp(data):return struct.pack('<I',len(data))+data


def digest(parts):
    chunks=[hashlib.sha256(b'\xa5'+struct.pack('<I',len(c))+c).digest()
            for part in parts for c in (part[i:i+1048576] for i in range(0,len(part),1048576))]
    return hashlib.sha256(b'\x5a'+struct.pack('<I',len(chunks))+b''.join(chunks)).digest()


def certificate_from_apk(apk):
    end=apk.rfind(b'PK\x05\x06');cd=struct.unpack_from('<I',apk,end+16)[0]
    block_size=struct.unpack_from('<Q',apk,cd-24)[0];start=cd-block_size-8
    pair_size=struct.unpack_from('<Q',apk,start+8)[0]
    assert struct.unpack_from('<I',apk,start+16)[0]==0x7109871a
    def read(v,p):
        n=struct.unpack_from('<I',v,p)[0];return v[p+4:p+4+n],p+4+n
    value=apk[start+20:start+16+pair_size];seq,_=read(value,0);signer,_=read(seq,0)
    signed,_=read(signer,0);dig,p=read(signed,0);certs,p=read(signed,p)
    cert,_=read(certs,0)
    return cert


def main():
    global SOURCE,FORMAL,KEY,OUT
    parser=argparse.ArgumentParser(description='Assemble V5.46 with the original formal signing identity')
    parser.add_argument('--base-apk',type=Path,default=SOURCE)
    parser.add_argument('--previous-apk',type=Path,default=FORMAL)
    parser.add_argument('--keystore',type=Path,default=KEY)
    parser.add_argument('--out',type=Path,default=OUT)
    args=parser.parse_args();SOURCE,FORMAL,KEY,OUT=args.base_apk,args.previous_apk,args.keystore,args.out
    for required in [SOURCE,FORMAL,KEY,ROOT/'native_build/dex/classes.dex']:
        if not required.is_file():parser.error('Missing build input: '+str(required))
    key,cert,_=pkcs12.load_key_and_certificates(KEY.read_bytes(),b'changeit')
    assert cert.public_bytes(serialization.Encoding.DER)==certificate_from_apk(FORMAL.read_bytes())
    assets=ROOT/'app/assets'
    raw_stream=io.BytesIO()
    with ZipFile(SOURCE) as original,ZipFile(raw_stream,'w') as dest:
        for item in original.infolist():
            if item.filename.startswith('assets/') and (assets/item.filename[7:]).is_file():continue
            if item.filename=='resources.arsc':
                item.extra=b''
                pos=raw_stream.tell()+30+len(item.filename.encode('utf-8'))
                pad=(-pos-4)%4
                item.extra=struct.pack('<HH',0xCAFE,pad)+bytes(pad)
                assert item.compress_type==0
            value=(modify_manifest(original.read(item.filename)) if item.filename=='AndroidManifest.xml'
                   else modify_resources(original.read(item.filename)) if item.filename=='resources.arsc'
                   else (assets/'index.html').read_bytes() if item.filename=='assets/index.html'
                   else (assets/'cloud.js').read_bytes() if item.filename=='assets/cloud.js'
                   else (ROOT/'native_build/dex/classes.dex').read_bytes() if item.filename=='classes2.dex'
                   else original.read(item.filename))
            dest.writestr(item,value)
        for asset in sorted(assets.iterdir()):
            if asset.is_file():
                info=ZipInfo('assets/'+asset.name);info.compress_type=ZIP_DEFLATED
                dest.writestr(info,asset.read_bytes())
    raw=raw_stream.getvalue();eocd=raw.rfind(b'PK\x05\x06');cd=struct.unpack_from('<I',raw,eocd+16)[0]
    assert raw[cd:cd+4]==b'PK\x01\x02'
    algorithm=0x0103
    digest_record=lp(struct.pack('<I',algorithm)+lp(digest((raw[:cd],raw[cd:eocd],raw[eocd:]))))
    signed=lp(digest_record)+lp(lp(cert.public_bytes(serialization.Encoding.DER)))+lp(b'')
    signature=key.sign(signed,padding.PKCS1v15(),hashes.SHA256())
    signer=(lp(signed)+lp(lp(struct.pack('<I',algorithm)+lp(signature)))
            +lp(key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)))
    signers=lp(signer)
    pair=struct.pack('<Q',len(lp(signers))+4)+struct.pack('<I',0x7109871a)+lp(signers)
    block_size=len(pair)+24
    block=struct.pack('<Q',block_size)+pair+struct.pack('<Q',block_size)+b'APK Sig Block 42'
    end=bytearray(raw[eocd:]);struct.pack_into('<I',end,16,cd+len(block))
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_bytes(raw[:cd]+block+raw[cd:eocd]+end)
    with ZipFile(OUT) as final,ZipFile(SOURCE) as original:
        assert final.testzip() is None
        for dex in ('classes.dex','classes2.dex'):
            assert final.read(dex)==((ROOT/'native_build/dex/classes.dex').read_bytes() if dex=='classes2.dex' else original.read(dex))
        info=final.getinfo('resources.arsc')
        filename,extra=struct.unpack_from('<HH',OUT.read_bytes(),info.header_offset+26)
        assert info.compress_type==0 and (info.header_offset+30+filename+extra)%4==0
    print(OUT,OUT.stat().st_size,cert.fingerprint(hashes.SHA256()).hex())


if __name__=='__main__':main()
