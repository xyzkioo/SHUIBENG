"""Build a native, dependency-free Android APK using official Android SDK tools."""
import os
from pathlib import Path
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parent
SDK=ROOT.parent/'.tools/android/sdk-selected'
JDK=Path(os.environ.get('PUMP_JDK', 'D:/Tools/PyCharm 2025.1.3.1/jbr'))
ANDROID=SDK/'android-35/android.jar'
TOOLS=SDK/'android-15'
BUILD=ROOT/'build'
DIST=ROOT/'dist'
for directory in ['generated','classes','dex','tests']:
    (BUILD/directory).mkdir(parents=True,exist_ok=True)
DIST.mkdir(exist_ok=True)

def run(args):
    print('RUN',Path(str(args[0])).name, ' '.join(str(x) for x in args[1:3]),flush=True)
    subprocess.run([str(x) for x in args],cwd=ROOT,check=True)

JAVA=JDK/'bin/java.exe'
JAVAC=JDK/'bin/javac.exe'
run([JAVAC,'-encoding','UTF-8','-d',BUILD/'tests',ROOT/'src/cn/pump/control/Protocol.java',ROOT/'tests/ProtocolTest.java'])
run([JAVA,'-cp',BUILD/'tests','cn.pump.control.ProtocolTest'])
run([TOOLS/'aapt2.exe','compile','--dir',ROOT/'res','-o',BUILD/'resources.zip'])
run([TOOLS/'aapt2.exe','link','-o',BUILD/'resource.apk','--manifest',ROOT/'AndroidManifest.xml',
     '-I',ANDROID,'--java',BUILD/'generated','--auto-add-overlay','-R',BUILD/'resources.zip'])
sources=list((ROOT/'src').rglob('*.java'))+list((BUILD/'generated').rglob('*.java'))
run([JAVAC,'-encoding','UTF-8','-source','8','-target','8','-Xlint:-options',
     '-bootclasspath',str(ANDROID)+os.pathsep+str(TOOLS/'core-lambda-stubs.jar'),'-d',BUILD/'classes',*sources])
classes=list((BUILD/'classes').rglob('*.class'))
run([JAVA,'-cp',TOOLS/'lib/d8.jar','com.android.tools.r8.D8','--lib',ANDROID,
     '--lib',TOOLS/'core-lambda-stubs.jar','--min-api','23','--output',BUILD/'dex',*classes])
with zipfile.ZipFile(BUILD/'resource.apk') as source, zipfile.ZipFile(BUILD/'unsigned.apk','w') as target:
    for item in source.infolist(): target.writestr(item,source.read(item.filename))
    for dex in (BUILD/'dex').glob('*.dex'):target.write(dex,dex.name,compress_type=zipfile.ZIP_DEFLATED)
run([TOOLS/'zipalign.exe','-f','4',BUILD/'unsigned.apk',BUILD/'aligned.apk'])
key=ROOT/'keystore/local-debug.p12'
key.parent.mkdir(exist_ok=True)
if not key.exists():
    run([JDK/'bin/keytool.exe','-genkeypair','-keystore',key,'-storepass','android','-keypass','android',
         '-alias','pump-local','-keyalg','RSA','-keysize','2048','-validity','3650',
         '-dname','CN=Pump Control,O=Local Development,C=CN'])
apk=DIST/'PumpControl.apk'
run([JAVA,'-jar',TOOLS/'lib/apksigner.jar','sign','--ks',key,'--ks-pass','pass:android',
     '--key-pass','pass:android','--ks-key-alias','pump-local','--out',apk,BUILD/'aligned.apk'])
run([JAVA,'-jar',TOOLS/'lib/apksigner.jar','verify','--verbose','--print-certs',apk])
run([TOOLS/'zipalign.exe','-c','4',apk])
with (DIST/'apk-info.txt').open('w',encoding='utf-8') as output:
    subprocess.run([str(TOOLS/'aapt2.exe'),'dump','badging',str(apk)],stdout=output,check=True)
print('BUILT',apk,apk.stat().st_size,'bytes',flush=True)
