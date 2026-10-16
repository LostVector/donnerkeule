from control import both, HOSTS, LAB, ROOT
import json, shlex, time

start='''import pathlib,re,subprocess
lab=pathlib.Path(LAB);unit=(lab/'private/service-name').read_text().strip()
assert re.fullmatch(r'gufo(?:-rank1)?@q8\\.service',unit), 'Unexpected saved unit'
state=subprocess.run(['systemctl','--user','show',unit,'--property=ActiveState','--value'],capture_output=True,text=True)
if state.stdout.strip()=='failed':
 subprocess.run(['systemctl','--user','reset-failed',unit],check=True)
subprocess.run(['systemctl','--user','start',unit],check=True)
print(unit+' started')
'''.replace('LAB',repr(LAB))
for h,p in both(dict.fromkeys(HOSTS,'python3 -c '+shlex.quote(start))).items():
    print(h,p.stdout,flush=True)

check='''import hashlib,json,pathlib,subprocess,urllib.request
lab=pathlib.Path(LAB);original=json.loads((lab/'private/gufo-inspect.json').read_text())
unit=(lab/'private/service-name').read_text().strip()
def run(args):return subprocess.run(args,capture_output=True,text=True)
ids=run(['podman','ps','--format','{{.ID}}']).stdout.split()
objects=[json.loads(subprocess.check_output(['podman','inspect',i]))[0] for i in ids]
same_image=[o for o in objects if o['Image']==original['Image']]
assert len(same_image)==1, 'Original image instance is not unique'
o=same_image[0]
def command(a):
 a=list(a)
 if '--tp-control-token' in a:a[a.index('--tp-control-token')+1]='<rotated-service-token>'
 return a
def environment(a):return dict(v.split('=',1) for v in a)
def mounts(a):return sorted(a,key=lambda v:(v.get('Destination',''),v.get('Source','')))
result={'service':unit,'service_active':run(['systemctl','--user','is-active',unit]).stdout.strip()=='active',
 'service_unit_unchanged':run(['systemctl','--user','cat',unit]).stdout==(lab/'private/service-unit.txt').read_text(),
 'service_enabled_unchanged':run(['systemctl','--user','is-enabled',unit]).stdout.strip()==(lab/'private/service-enabled').read_text().strip(),
 'image_matches':o['Image']==original['Image'],'container_running':o['State']['Running'],
 'container_configuration_matches':{},'host_configuration_matches':{},
 'mounts_match':mounts(o['Mounts'])==mounts(original['Mounts']),
 'test_containers_remaining':[v['Name'] for v in objects if v['Name'].removeprefix('/').startswith('tbv-cq-')],
 'image_id':o['Image'],'pid':o['State']['Pid'],
 'control_token_rotated':o['Config']['Cmd']!=original['Config']['Cmd']}
for key in ('Cmd','Env','Entrypoint','WorkingDir','User'):
 a=o['Config'].get(key);b=original['Config'].get(key)
 if key=='Cmd':a,b=command(a),command(b)
 if key=='Env':a,b=environment(a),environment(b)
 result['container_configuration_matches'][key]=a==b
for key in ('Binds','Devices','DeviceCgroupRules','GroupAdd','SecurityOpt','NetworkMode','IpcMode','Privileged','CapAdd','CapDrop','ReadonlyRootfs','AutoRemove','ShmSize','Memory','PidsLimit','RestartPolicy','LogConfig'):
 a=o['HostConfig'].get(key);b=original['HostConfig'].get(key)
 if key=='Binds':a,b=sorted(a),sorted(b)
 result['host_configuration_matches'][key]=a==b
if '--tp-rank' in original['Config']['Cmd']:
 rank=int(original['Config']['Cmd'][original['Config']['Cmd'].index('--tp-rank')+1])
else:raise AssertionError('Missing original TP rank')
if rank==0:
 try:
  with urllib.request.urlopen('http://127.0.0.1:8000/ready',timeout=2) as response:result['http_ready']=response.status==200
 except Exception:result['http_ready']=False
else:result['http_ready']=None
(lab/'results/restored-gufo.json').write_text(json.dumps(result,indent=2)+'\\n')
token=o['Config']['Cmd'][o['Config']['Cmd'].index('--tp-control-token')+1]
result['_token_fingerprint']=hashlib.sha256(token.encode()).hexdigest()
print(json.dumps(result))
'''.replace('LAB',repr(LAB))
deadline=time.monotonic()+240
while time.monotonic()<deadline:
    values=both(dict.fromkeys(HOSTS,'python3 -c '+shlex.quote(check)))
    results={h:json.loads(p.stdout) for h,p in values.items()}
    tokens=[o.pop('_token_fingerprint') for o in results.values()]
    assert len(set(tokens))==1, 'Live rank control tokens differ'
    for o in results.values():o['rank_control_tokens_match']=True
    for h,o in results.items():
        (ROOT/(h+'-restored-gufo.json')).write_text(json.dumps(o,indent=2)+'\n')
    if all(o['service_active'] and o['container_running'] and o['http_ready'] is not False for o in results.values()):
        for h,o in results.items():
            assert o['service_unit_unchanged'] and o['service_enabled_unchanged'] and o['mounts_match']
            assert all(o['container_configuration_matches'].values()) and all(o['host_configuration_matches'].values())
            assert not o['test_containers_remaining']
            print(h,json.dumps(o),flush=True)
        print('Original supervised Gufo services ready and configuration verified',flush=True)
        break
    time.sleep(3)
else:raise RuntimeError('Original Gufo readiness timeout; services remain started')
