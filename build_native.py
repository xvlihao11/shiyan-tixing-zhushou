"""Compile Java sources and convert them into fresh DEX (requires android.jar and r8.jar)."""
from pathlib import Path
import argparse,subprocess
p=argparse.ArgumentParser();p.add_argument('--android-jar',required=True);p.add_argument('--r8-jar',required=True);p.add_argument('--java',default='java');args=p.parse_args()
root=Path(__file__).resolve().parent;classes=root/'native_build/classes';dex=root/'native_build/dex';classes.mkdir(parents=True,exist_ok=True);dex.mkdir(parents=True,exist_ok=True)
subprocess.run([args.java,'com.sun.tools.javac.Main','-encoding','UTF-8','-source','8','-target','8','-classpath',args.android_jar,'-d',str(classes),*[str(f) for f in (root/'app/src').rglob('*.java')]],check=True)
subprocess.run([args.java,'-cp',args.r8_jar,'com.android.tools.r8.D8','--lib',args.android_jar,'--min-api','26','--output',str(dex),*[str(f) for f in classes.rglob('*.class')]],check=True)
