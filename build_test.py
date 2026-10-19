"""Compile and sign the independent Mode1 test app. Private keys stay external."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse, subprocess, sys

def main():
    parser = argparse.ArgumentParser()
    for name in ('java', 'android-jar', 'build-tools', 'keystore', 'password-file', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    java, android, tools = args.java.resolve(), args.android_jar.resolve(), args.build_tools.resolve()
    key, password, out = args.keystore.resolve(), args.password_file.resolve(), args.out.resolve()
    build = root / 'test_build'
    build.mkdir(exist_ok=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    def run(cmd):
        subprocess.run([str(x) for x in cmd], cwd=root, check=True)
    run([sys.executable, root/'build_native.py', '--java', java, '--android-jar', android, '--r8-jar', tools/'lib/d8.jar'])
    run([tools/'aapt2.exe', 'compile', '--dir', root/'app/res', '-o', build/'resources.zip'])
    run([tools/'aapt2.exe', 'link', '-I', android, '--manifest', root/'app/AndroidManifest.xml', '-A', root/'app/assets', '-o', build/'resources.apk', build/'resources.zip'])
    with ZipFile(build/'resources.apk') as src, ZipFile(build/'unsigned.apk', 'w', ZIP_DEFLATED) as dst:
        for item in src.infolist():
            dst.writestr(item, src.read(item.filename))
        for dex in sorted((root/'native_build/dex').glob('classes*.dex')):
            dst.write(dex, dex.name)
    run([tools/'zipalign.exe', '-f', '-p', '4', build/'unsigned.apk', build/'aligned.apk'])
    run([java, '-jar', tools/'lib/apksigner.jar', 'sign', '--ks', key, '--ks-key-alias', 'mode1-test', '--ks-pass', 'file:'+str(password), '--v1-signing-enabled', 'true', '--v2-signing-enabled', 'true', '--out', out, build/'aligned.apk'])
    run([java, '-jar', tools/'lib/apksigner.jar', 'verify', '--verbose', '--print-certs', out])
    run([tools/'zipalign.exe', '-c', '-p', '4', out])
    # Verify the delivered APK, not only the source tree or staging directory.
    with ZipFile(out) as package:
        for asset in (root/'app/assets').rglob('*'):
            if asset.is_file():
                entry='assets/'+asset.relative_to(root/'app/assets').as_posix()
                if package.read(entry)!=asset.read_bytes():
                    raise RuntimeError('APK asset does not match build source: '+entry)
    run([tools/'aapt2.exe', 'dump', 'badging', out])

if __name__ == '__main__':
    main()
