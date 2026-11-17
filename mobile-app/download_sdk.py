"""Download only official Android platform/build-tools, with resume and repo checksums."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import subprocess
import time
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[1] / '.tools/android'
repo = ET.parse(root / 'repository.xml').getroot()
packages = [('platforms;android-35', 'platform-35_r02.zip', 'platform-35.zip'),
            ('build-tools;35.0.0', 'build-tools_r35_windows.zip', 'build-tools-35.zip')]

def download(item):
    package, remote, local = item
    archive = next(a.find('complete') for p in repo.findall('remotePackage')
                   if p.attrib['path'] == package for a in p.findall('archives/archive')
                   if a.findtext('complete/url') == remote)
    size = int(archive.findtext('size'))
    checksum = archive.findtext('checksum')
    destination = root / local
    for attempt in range(40):
        existing = destination.stat().st_size if destination.exists() else 0
        if existing == size:
            actual = hashlib.sha1(destination.read_bytes()).hexdigest()
            if actual != checksum:
                raise RuntimeError(f'{local}: official SHA1 mismatch {actual} != {checksum}')
            with zipfile.ZipFile(destination) as zipped:
                zipped.extractall(root / 'sdk')
            print(f'VERIFIED AND EXTRACTED {local}', flush=True)
            return
        if existing > size:
            raise RuntimeError(f'{local}: unexpected size {existing}')
        subprocess.run(['curl.exe', '-sS', '-L', '--ssl-no-revoke', '-C', '-',
                        '--connect-timeout', '15', '--max-time', '60',
                        '-o', str(destination), 'https://dl.google.com/android/repository/' + remote],
                       check=False)
        current = destination.stat().st_size if destination.exists() else 0
        print(f'{local}: {current}/{size} bytes, pass {attempt+1}', flush=True)
        time.sleep(1)
    raise RuntimeError(f'{local}: network resume limit reached')

with ThreadPoolExecutor(max_workers=2) as executor:
    list(executor.map(download, packages))
