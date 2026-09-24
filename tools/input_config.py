#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate independent input-only kvmd configuration (JSON is valid YAML)."""
import argparse,json
from pathlib import Path

def configuration(source, state, user, udc):
    source=str(Path(source).resolve());state=str(Path(state).resolve())
    return {
      'kvmd': {
        'auth': {'enabled':True,'internal':{'type':'htpasswd','file':state+'/htpasswd'},
                 'totp':{'secret':{'file':''}},'usc':{'users':[user],'groups':[], 'kvmd_users':[], 'kvmd_groups':[]}},
        'atx':{'type':'disabled'},'msd':{'type':'disabled'},
        'hid':{'type':'otg','keymap':source+'/contrib/keymaps/en-us',
               'keyboard':{'device':'/dev/hidg0'},
               'mouse':{'device':'/dev/hidg1','absolute':False,'horizontal_wheel':True},
               'mouse_alt':{'device':'/dev/hidg2','horizontal_wheel':True},'jiggler':{'enabled':False}},
        'server':{'unix':'/run/rkmoon-input/kvmd.sock','unix_mode':0o660,'unix_rm':True},
        'info':{'extras':state+'/extras','meta':state+'/meta.yaml',
                'hw':{'platform':state+'/platform','vcgencmd_cmd':['/bin/false']}},
        'log_reader':{'enabled':False},'gpio':{'drivers':{},'scheme':{}},
        'snapshot':{'idle_interval':0,'live_interval':0,'wakeup_key':'','wakeup_move':0},
        'streamer':{'cmd':['/bin/false'],'forever':False,'quality':0,
                    'unix':'/run/rkmoon-input/unused-streamer.sock',
                    'process_name_prefix':'rkmoon/unused-streamer'},
        'switch':{'device':'/dev/rkmoon-no-switch','default_edid':source+'/configs/kvmd/edid/v0.hex'}},
      'otg':{'gadget':'rkmoon','udc':udc,'user':'root','meta':'/run/rkmoon-gadget/meta',
             'manufacturer':'RKMoon','product':'RKMoon keyboard and dual-mode mouse',
             'serial':'RKMOON001','config':'RKMoon HID only','remote_wakeup':False,
             'devices':{'hid':{'keyboard':{'start':True},'mouse':{'start':True},'mouse_alt':{'start':True}},
                        'msd':{'start':False},'drives':{'enabled':False},'audio':{'enabled':False},
                        'ethernet':{'enabled':False},'serial':{'enabled':False}}},
      'pst':{'server':{'unix':'/run/rkmoon-input/unused-pst.sock'}}}

def write_config(source,state,user,udc):
    state=Path(state);state.mkdir(parents=True,exist_ok=True)
    for name in ['extras','override.d']: (state/name).mkdir(exist_ok=True)
    for name,content in {'htpasswd':'','empty.yaml':'{}\n','meta.yaml':'{}\n','platform':'RKMOON_BOARD=NanoPC-T6\n','unused-edid.hex':''}.items():
        p=state/name
        if not p.exists():p.write_text(content)
    p=state/'main.yaml'
    if p.exists():raise RuntimeError('Refusing existing configuration; back up and migrate explicitly')
    p.write_text(json.dumps(configuration(source,state,user,udc),indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True);p.add_argument('--state',required=True)
    p.add_argument('--user',required=True);p.add_argument('--udc',required=True)
    a=p.parse_args();write_config(a.source,a.state,a.user,a.udc)
