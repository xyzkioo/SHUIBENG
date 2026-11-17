import pathlib
import struct
import zlib
root = pathlib.Path(__file__).resolve().parents[1] / '.tools/android'
for path in root.glob('*.zip'):
    data = path.read_bytes()
    offset = 0
    print(path.name, len(data))
    while offset + 30 < len(data) and data[offset:offset+4] == b'PK\x03\x04':
        header = struct.unpack_from('<IHHHHHIIIHH', data, offset)
        name = data[offset+30:offset+30+header[9]].decode(errors='replace')
        print(name, header[7], header[8], offset)
        if header[2] & 8:
            print('data descriptor'); break
        offset += 30 + header[9] + header[10] + header[7]
