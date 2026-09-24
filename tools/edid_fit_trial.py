#!/usr/bin/env python3
"""Offline capacity trial only. Never installs or claims sink capability."""
import argparse
import json
import struct
from pathlib import Path

def generate(base, modes):
    b = bytearray(base[:128])
    assert len(b) == 128 and b[:8] == bytes.fromhex('00ffffffffffff00')
    # Clear inherited established/standard modes; preserve identity/chromaticity.
    b[35:38] = bytes(3)
    b[38:54] = bytes([1, 1]) * 8
    # Keep original preferred DTD, name, and blank other descriptors.
    preferred = bytes(b[54:72])
    b[72:126] = (bytes.fromhex('0000001000') + bytes(13)) * 3
    b[72:90] = bytes.fromhex('000000fc00') + b'RKMoon Trial\n'
    b[126] = 1
    b[127] = -sum(b[:127]) & 255
    vics = {(1920,1080,30):34, (1920,1080,60):16,
            (1920,1080,120):63, (3840,2160,30):95, (3840,2160,60):97}
    standard = [vics[m] for m in modes if m in vics]
    custom = [m for m in modes if m not in vics]
    data = bytearray([0x40 | len(standard), *standard])
    # LPCM stereo 32/44.1/48kHz 16-bit, speaker FL/FR, HDMI 600MHz + SCDC.
    data += bytes.fromhex('230907018301000067030c001000007867d85dc401788000')
    for start in range(0, len(custom), 4):
        payload = bytearray([0x2a, 0])
        for w,h,f in custom[start:start+4]:
            payload += struct.pack('<BHHB', 1, w-1, h-1, f-1)
        data += bytes([0xe0 | len(payload)]) + payload
    assert len(data) <= 123
    ext = bytearray([2,3,4+len(data),0x40]) + data
    ext += bytes(127-len(ext))
    ext += bytes([-sum(ext) & 255])
    out = bytes(b+ext)
    assert len(out) == 256 and all(sum(out[i:i+128]) % 256 == 0 for i in (0,128))
    # Independently walk the produced CTA data blocks and recover all modes.
    decoded=[]; pos=4
    reverse={v:k for k,v in vics.items()}
    while pos < ext[2]:
        header=ext[pos]; length=header&31; p=ext[pos+1:pos+1+length]
        if header>>5 == 2: decoded += [reverse[v] for v in p]
        if header>>5 == 7 and p[0] == 0x2a:
            for n in range(2,len(p),6):
                flags,w,h,f=struct.unpack('<BHHB',p[n:n+6])
                assert flags==1
                decoded.append((w+1,h+1,f+1))
        pos += 1+length
    assert sorted(decoded) == sorted(modes)
    return out, dict(total_bytes=256,requested_modes=len(modes),vic_modes=len(standard),
        type_x_modes=len(custom),cta_data_bytes=len(data),cta_unused_bytes=123-len(data),
        checksums_valid=True,roundtrip_modes_valid=True,
        status='offline-capacity-trial-not-deployed',
        limitations=['Type X support on actual source devices is untested',
        'Inherited base preferred DTD may add a duplicate or extra mode',
        'HDMI audio/link declarations are trial values, not hardware acceptance',
        'No receiver, encoder or client playback validation'])

if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('base_json'); p.add_argument('output')
    a=p.parse_args()
    source=json.loads(Path(a.base_json).read_text())
    base=bytes.fromhex(source['edid_hex'])
    spec=json.loads(Path('config/edid-requested-modes.json').read_text())
    modes=[(m['width'],m['height'],f) for m in spec['modes'] for f in m['refresh_hz']]
    binary,report=generate(base,modes)
    Path(a.output).write_bytes(binary)
    Path(a.output+'.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
