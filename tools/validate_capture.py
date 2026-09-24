#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent decode + recorded pipeline statistics. This never certifies physical end-to-end latency."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess

def file_sha256(path):
    digest=hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda:f.read(1024*1024),b""):digest.update(block)
    return digest.hexdigest()

def quantiles(values):
    values=sorted(values)
    if not values:return {'n':0}
    def q(p):
        x=(len(values)-1)*p;i=int(x);return values[i]+(values[min(i+1,len(values)-1)]-values[i])*(x-i)
    return {'n':len(values),'p50':q(.5),'p95':q(.95),'p99':q(.99),'max':values[-1]}

def validate(path,stats,codec,width,height,fps,seconds,expected_range=None,expected_transfer=None):
    result={'schema':1,'evidence_kind':'independent_bitstream_decode_and_csv_checks','hardware_acceptance':'NOT_ASSIGNED',
            'physical_display_latency':'NOT_MEASURED','file_sha256':file_sha256(path),'checks':{},'limitations':[]}
    entries='codec_name,profile,level,width,height,pix_fmt,color_range,color_space,color_transfer,color_primaries,has_b_frames,nb_read_frames'
    probe=subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-count_frames','-show_entries','stream='+entries,'-of','json',str(path)],capture_output=True,text=True,timeout=180)
    if probe.returncode:raise RuntimeError('ffprobe failed; inspect bitstream with independent decoder')
    streams=json.loads(probe.stdout).get('streams',[])
    if len(streams)!=1:raise RuntimeError('expected one video stream')
    stream=streams[0];result['stream']=stream;check=result['checks']
    check['codec']=stream.get('codec_name')==codec
    check['dimensions']=(stream.get('width'),stream.get('height'))==(width,height)
    check['eight_bit_420']=stream.get('pix_fmt') in {'yuv420p','yuvj420p'}
    check['profile']=stream.get('profile')=='Main' if codec=='hevc' else stream.get('profile') in {'Baseline','Constrained Baseline'}
    check['no_reported_b_frames']=stream.get('has_b_frames')==0
    check['bt709_matrix']=stream.get('color_space')=='bt709'
    check['bt709_primaries']=stream.get('color_primaries')=='bt709'
    if expected_range:check['range_matches_source']=stream.get('color_range')==expected_range
    else:result['limitations'].append('No independently established source range supplied; source/VUI agreement is not certified.')
    if expected_transfer:check['transfer_matches_source']=stream.get('color_transfer')==expected_transfer
    else:result['limitations'].append('No source transfer function supplied; source/VUI agreement is not certified.')
    # Raw Annex-B carries no container timestamps; give the demuxer the nominal rate so
    # high-fps streams do not produce muxer DTS warnings that are unrelated to decoding.
    decode=subprocess.run(['ffmpeg','-v','error','-xerror','-threads','2','-f',codec,'-framerate',f'{fps:g}','-i',str(path),'-map','0:v:0','-f','null','-'],capture_output=True,text=True,timeout=180)
    check['independent_complete_decode']=decode.returncode==0 and not decode.stderr.strip()
    result['decoder_diagnostics']=decode.stderr[:4000]
    with stats.open(newline='') as f:rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(f)]
    if len(rows)<2:raise RuntimeError('not enough statistics samples')
    check['csv_sequence_contiguous']=all(r['seq']==i+1 for i,r in enumerate(rows))
    check['stage_order']=all(r['dequeue_us']<=r['submit_us']<=r['done_us'] for r in rows)
    check['timestamps_monotonic']=all(a['dequeue_us']<b['dequeue_us'] for a,b in zip(rows,rows[1:]))
    check['decoded_frame_count_matches_csv']=int(stream.get('nb_read_frames','0'))==len(rows)
    elapsed=(rows[-1]['dequeue_us']-rows[0]['dequeue_us'])/1e6
    actual_fps=(len(rows)-1)/elapsed if elapsed>0 else 0
    result['observed']={'frames':len(rows),'dequeue_span_s':elapsed,'fps_from_dequeue_span':actual_fps,
                        'average_payload_bitrate_bps':sum(r['bytes'] for r in rows)*8/max(elapsed,1e-6),
                        'idr_sequences':[r['seq'] for r in rows if r['idr']],
                        'raw_skipped':rows[-1]['raw_skipped'],'paths':sorted(set('dmabuf' if r['dmabuf'] else 'cpu_pixel_copy' for r in rows))}
    check['required_duration']=elapsed>=max(0,seconds-1)
    check['cadence_within_5_percent']=abs(actual_fps-fps)<=fps*.05
    check['first_frame_idr']=rows[0]['idr']==1
    result['latency_us']={'dequeue_to_submit':quantiles([r['submit_us']-r['dequeue_us'] for r in rows]),
                          'submit_to_au':quantiles([r['done_us']-r['submit_us'] for r in rows]),
                          'dequeue_to_au':quantiles([r['done_us']-r['dequeue_us'] for r in rows])}
    resource_file=stats.parent/'resources.jsonl'
    command_file=stats.parent/'command.json'
    if resource_file.exists() and command_file.exists():
        samples=[json.loads(line) for line in resource_file.read_text().splitlines() if line.strip()]
        hz=json.loads(command_file.read_text()).get('cpu_ticks_per_second',0)
        cpu=[]
        if isinstance(hz,(int,float)) and hz>0:
            for a,b in zip(samples,samples[1:]):
                dt=b['monotonic_s']-a['monotonic_s']
                if dt>0:cpu.append(100*(b['cpu_ticks']-a['cpu_ticks'])/hz/dt)
        result['resources']={'cpu_percent_one_core_100':quantiles(cpu),
            'rss_bytes':quantiles([s['rss_bytes'] for s in samples]),
            'thermal_zone_millidegrees':quantiles([t for s in samples for t in s['temperature_millidegrees']]),
            'note':'thermal zones are pooled here; match zone type using the separate read-only baseline'}
    result['limitations'].append('CSV proves neither HDMI full capture delay nor network/client/USB behavior. Inspect source content, thermals, IDR control timing and Moonlight separately.')
    result['all_supplied_checks_passed']=all(check.values())
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('bitstream',type=Path);p.add_argument('stats',type=Path)
    p.add_argument('--codec',choices=['hevc','h264'],required=True);p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080)
    p.add_argument('--fps',type=float,default=60);p.add_argument('--seconds',type=float,default=60)
    p.add_argument('--expected-range',choices=['tv','pc']);p.add_argument('--expected-transfer',choices=['bt709','iec61966-2-1'])
    p.add_argument('--output',type=Path);a=p.parse_args()
    r=validate(a.bitstream,a.stats,a.codec,a.width,a.height,a.fps,a.seconds,a.expected_range,a.expected_transfer)
    text=json.dumps(r,ensure_ascii=False,indent=2)+'\n'
    if a.output:
        with a.output.open('x') as f:f.write(text)
    else:print(text,end='')
    return 0 if r['all_supplied_checks_passed'] else 1
if __name__=='__main__':
    try:raise SystemExit(main())
    except (RuntimeError,OSError,ValueError,subprocess.TimeoutExpired) as e:raise SystemExit(str(e))
