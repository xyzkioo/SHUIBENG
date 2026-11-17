"""Range-extract needed files from official Android SDK archives; verify ZIP CRCs."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import binascii
import struct
import time
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[1] / '.tools/android'
NEEDED = {'android.jar', 'aapt2.exe', 'zipalign.exe', 'd8.jar', 'apksigner.jar',
          'core-lambda-stubs.jar', 'libwinpthread-1.dll', 'libc++.dll'}

def package(remote, local, size):
    url = 'https://dl.google.com/android/repository/' + remote
    regions = []
    prefix = ROOT / local
    def get(start, length):
        if prefix.exists() and prefix.stat().st_size >= start + length:
            with prefix.open('rb') as stream:
                stream.seek(start); return stream.read(length)
        for offset, data in list(regions):
            if start >= offset and start + length <= offset + len(data):
                return data[start-offset:start-offset+length]
        cache = ROOT / f'{local}-{start}-{length}.part'
        data = cache.read_bytes() if cache.exists() else b''
        if prefix.exists() and prefix.stat().st_size > start + len(data):
            with prefix.open('rb') as stream:
                stream.seek(start); data = stream.read(min(length,prefix.stat().st_size-start))
            cache.write_bytes(data)
        if len(data) == length: return data
        if data: regions.append((start,data))
        if length > 1048576:
            pieces = [(start+i,min(1048576,length-i)) for i in range(0,length,1048576)]
            with ThreadPoolExecutor(max_workers=4) as executor:
                data = b''.join(executor.map(lambda piece: get(*piece),pieces))
            cache.write_bytes(data)
            return data
        for attempt in range(50):
            if len(data) == length: return data
            at = start + len(data)
            request = urllib.request.Request(url, headers={'Range': f'bytes={at}-{start+length-1}'})
            try:
                with urllib.request.urlopen(request, timeout=20) as response:
                    if response.status != 206 or not response.headers.get('Content-Range','').startswith(f'bytes {at}-'):
                        raise RuntimeError('Server did not honor requested range')
                    with cache.open('ab') as output:
                        while len(data) < length:
                            chunk = response.read(min(65536, length-len(data)))
                            if not chunk: break
                            output.write(chunk); data += chunk
            except Exception as error:
                print(remote, start, len(data), str(error)[:80], flush=True)
            if attempt % 5 == 0: print(f'{remote} range {start}: {len(data)}/{length}', flush=True)
            time.sleep(.2)
        raise RuntimeError(f'Range failed: {start}, {length}')
    tail_start = size - 65557
    tail = get(tail_start, 65557)
    regions.append((tail_start, tail))
    end = tail.rfind(b'PK\x05\x06')
    fields = struct.unpack_from('<4s4H2LH', tail, end)
    central = get(fields[6], fields[5])
    entries = []
    offset = 0
    while offset < len(central):
        record = struct.unpack_from('<4s6H3L5H2L', central, offset)
        assert record[0] == b'PK\x01\x02'
        name = central[offset+46:offset+46+record[10]].decode()
        if Path(name).name in NEEDED:
            entries.append((name,record))
        offset += 46 + record[10] + record[11] + record[12]
    print('SELECTED', remote, [name for name, _ in entries], flush=True)
    def extract(entry):
        name, record = entry
        target = ROOT / 'sdk-selected' / name
        if target.exists() and binascii.crc32(target.read_bytes()) & 0xffffffff == record[7]:
            print('CACHED',name,flush=True); return
        header = get(record[16], 30)
        local_record = struct.unpack('<4s5H3L2H', header)
        begin = record[16] + 30 + local_record[9] + local_record[10]
        compressed = get(begin, record[8])
        data = zlib.decompress(compressed, -15) if record[4] == 8 else compressed
        assert len(data) == record[9] and binascii.crc32(data) & 0xffffffff == record[7], name
        target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        print('EXTRACTED',name,len(data),flush=True)
    with ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(extract, entries))

with ThreadPoolExecutor(max_workers=2) as executor:
    list(executor.map(lambda args: package(*args), [
        ('platform-35_r02.zip','platform-35.zip',64273788),
        ('build-tools_r35_windows.zip','build-tools-35.zip',59878107)]))
