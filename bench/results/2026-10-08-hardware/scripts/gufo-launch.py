#!/usr/bin/env python3
import json,pathlib,subprocess,sys
lab=pathlib.Path('/home/sn/tbv/hardware-20261007-xdomain-done')
o=json.loads((lab/'private/gufo-inspect.json').read_text())
original=o['Config']['CreateCommand'];cmd=o['Config']['Cmd']
candidates={o['ImageName'],o['Image'],'sha256:'+o['Image'].removeprefix('sha256:')}
indexes=[i for i,x in enumerate(original) if x in candidates];assert len(indexes)==1
prefix=original[2:indexes[0]]
prefix=[x for x in prefix if x!='--rm' and not x.startswith('--rm=')]
assert not any(x=='--name' or x.startswith('--name=') for x in prefix)
# Save logs in Podman's regular file log instead of the live service's none.
i=0
while i<len(prefix):
 if prefix[i]=='--log-driver': del prefix[i:i+2];continue
 if prefix[i].startswith('--log-driver='): del prefix[i];continue
 i+=1
env=dict(v.split('=',1) for v in o['Config']['Env'])
for i,x in enumerate(prefix):
 if x in ('-e','--env') and i+1<len(prefix) and '=' not in prefix[i+1]:
  prefix[i+1]=prefix[i+1]+'='+env[prefix[i+1]]
mode=sys.argv[1];budget=int(sys.argv[2]);assert budget in (0,50,5000)
rank=int(cmd[cmd.index('--tp-rank')+1]);name=f'tbv-cq-{mode}-{rank}-{budget}'
if mode=='serve':
 newcmd=['/lab/gufo']+cmd[1:]
 def option(flag,value):
  if flag in newcmd:newcmd[newcmd.index(flag)+1]=str(value)
  else:newcmd.extend([flag,str(value)])
 # Every launch gets the same bounded context and empty private cache.
 option('--context',32768);option('--sessions',1)
 option('--think','off')
 option('--port',19490);option('--tp-bootstrap-port',19491);option('--tp-control-port',19492)
 option('--tp-cq-spin-us',budget);option('--tp-rdma-device','usb4_rdma0')
 option('--cache-ram-bytes',1)
 if rank:option('--tp-bootstrap-host','192.168.211.2')
 for flag in ('--cache-disk','--cache-disk-bytes'):
  if flag in newcmd:
   i=newcmd.index(flag);del newcmd[i:i+2]
 # Remove write access to live cache mounts; test cache is in RAM only.
 for i,x in enumerate(prefix):
  if ':/cache' in x or ':/xdg-cache' in x:
   fields=x.split(':')
   if len(fields)==2:fields.append('ro')
   else:fields[-1]=','.join([v for v in fields[-1].split(',') if v!='rw']+['ro'])
   prefix[i]=':'.join(fields)
 mounts=['-v',str(lab/'gufo')+':/lab/gufo:ro']
elif mode=='probe':
 newcmd=['/lab/probe','--rank',str(rank),'--transport','verbs','--rdma-device','usb4_rdma0','--iterations','128','--cq-spin-us',str(budget)]
 if rank:newcmd+=['--connect','192.168.211.2']
 mounts=['-v',str(lab/'cq-probe')+':/lab/probe:ro']
else:sys.exit('Unknown test mode')
args=['podman','run','--detach','--name',name,'--log-driver=k8s-file']+prefix+mounts+[o['Image']]+newcmd
p=subprocess.run(args,capture_output=True,text=True)
if p.returncode:
 (lab/'private'/f'{name}-launch-error.log').write_text(p.stderr)
 sys.exit('Gufo test launch failed; private diagnostic saved')
(lab/'private'/f'{name}-container-id').write_text(p.stdout.strip()+'\n')
print(name)
