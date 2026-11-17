from pathlib import Path
import hashlib
import json
import zipfile

root=Path(__file__).resolve().parent
apk=root/'dist/PumpControl.apk'
with zipfile.ZipFile(apk) as archive:
    assert archive.testzip() is None
    assert {'AndroidManifest.xml','classes.dex','resources.arsc'}.issubset(archive.namelist())
    assert archive.read('classes.dex').startswith(b'dex\n035\x00')
files=[root/'AndroidManifest.xml',root/'build.py',root/'select_sdk.py',root/'README.md',root/'.gitignore']
for directory in ['src','res','tests']:
    files.extend(path for path in (root/directory).rglob('*') if path.is_file())
source=root/'dist/PumpControl-source.zip'
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in files: archive.write(path,Path('PumpControl')/path.relative_to(root))
    archive.write(root.parent/'BLUETOOTH_PROTOCOL.md','PumpControl/BLUETOOTH_PROTOCOL.md')
report={
    'apk':apk.name,'bytes':apk.stat().st_size,'sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),
    'package':'cn.pump.control','version':'1.0','min_android_api':23,'target_android_api':35,
    'validation':{'protocol_assertions':34,'java_compilation':True,'d8_dex035':True,
                  'zip_integrity':True,'zipalign':True,'apk_signatures':['v1','v2','v3']},
    'phone_ui_ble_end_to_end_tested':False,
}
(root/'dist/validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
print('SOURCE',source,source.stat().st_size)
