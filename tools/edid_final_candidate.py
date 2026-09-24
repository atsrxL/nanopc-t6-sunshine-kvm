#!/usr/bin/env python3
"""Generate the eight-mode OFFLINE candidate; never writes a device."""
import json, math, struct, sys
from pathlib import Path
from edid_fit_trial import generate

def dtd(w,h,f,standard=False):
    vs=5 if w*9==h*16 else 6 if w*10==h*16 else 10
    vb=max(math.floor(460/((1e6/f-460)/h))+1,3+vs+6)
    hb,hfp,hs,vfp=160,48,32,3
    clock=math.floor(f*(w+hb)*(h+vb)/250000)*25
    if standard:
        assert (w,h)==(1920,1080) and f in (60,120)
        clock,hb,vb,hfp,hs,vfp,vs=14850*f//60,280,45,88,44,4,5
    b=bytearray(18);struct.pack_into('<H',b,0,clock)
    b[2:8]=bytes([w&255,hb&255,((w>>8)<<4)|(hb>>8),h&255,vb&255,((h>>8)<<4)|(vb>>8)])
    b[8:12]=bytes([hfp&255,hs&255,(vfp<<4)|vs,0])
    b[17]=0x1e if standard else 0x1a
    return bytes(b)

source=json.loads(Path(sys.argv[1]).read_text())
spec=json.loads(Path('config/edid-requested-modes.json').read_text())
modes=[(m['width'],m['height'],f) for m in spec['modes'] for f in m['refresh_hz']]
out,_=generate(bytes.fromhex(source['edid_hex']),modes)
preferred=spec['preferred_mode']
b=bytearray(out[:128]);b[54:72]=dtd(preferred['width'],preferred['height'],preferred['refresh_hz'],True)
b[127]=-sum(b[:127])&255
vics={(1920,1080,30):34,(1920,1080,60):16,(1920,1080,120):63,(3840,2160,30):95,(3840,2160,60):97}
data=bytes([0x40|len(vics),*vics.values()])+bytes.fromhex('230907018301000067030c001000007867d85dc401788000')
custom=[m for m in modes if m not in vics]
ext=bytearray([2,3,4+len(data),0x40])+data
actual=[]
for w,h,f in custom:
    t=dtd(w,h,f);ext+=t
    aw=t[2]+((t[4]>>4)<<8);ah=t[5]+((t[7]>>4)<<8)
    ht=aw+t[3]+((t[4]&15)<<8);vt=ah+t[6]+((t[7]&15)<<8)
    hz=int.from_bytes(t[:2],'little')*10000/(ht*vt)
    assert (aw,ah)==(w,h) and abs(hz-f)<0.2
    actual.append(dict(width=aw,height=ah,requested_hz=f,actual_hz=hz))
remaining=127-len(ext);assert remaining>=0
ext+=bytes(remaining);ext+=bytes([-sum(ext)&255]);out=bytes(b+ext)
assert len(out)==256 and sum(b)%256==sum(ext)%256==0
Path(sys.argv[2]).write_bytes(out)
report=dict(status='offline-not-deployed',bytes=256,mode_count=len(modes),
    vic_count=len(vics),cta_dtd_count=len(custom),cta_unused_bytes=remaining,
    candidate_preferred='1920x1080@120 (user-selected)',
    detailed_timings=actual,checksums_valid=True,
    limitations=['No external conformance validation or hardware test',
                'HDMI/audio capabilities require live receiver verification',
                'Current server does not support every advertised mode'])
Path(sys.argv[2]+'.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
