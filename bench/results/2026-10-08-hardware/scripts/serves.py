from control import both,ssh,HOSTS,LAB,ROOT
import json, shlex,time
allrows=[];words=2000
for run,budget in enumerate((5000,50,0,5000)):
 names={h:f'tbv-cq-serve-{i}-{budget}' for i,h in enumerate(HOSTS)}
 try:
  for h in HOSTS:print(ssh(h,f'python3 {LAB}/gufo-launch.py serve {budget}').stdout,flush=True)
  deadline=time.monotonic()+240
  while time.monotonic()<deadline:
   p=ssh('fuzzy','curl --silent --max-time 2 --output /dev/null --write-out "%{http_code}" http://127.0.0.1:19490/ready',False)
   if p.stdout=='200':break
   state=both({h:f'podman inspect {names[h]} --format "{{{{.State.Running}}}}"' for h in HOSTS})
   if any(p.stdout.strip()!='true' for p in state.values()):raise RuntimeError('Gufo test server exited before ready')
   time.sleep(2)
  else:raise RuntimeError('Gufo readiness timeout')
  print('READY server budget',budget,flush=True)
  def cpu():
   code='''import json,os,pathlib,subprocess
name=NAME
o=json.loads(subprocess.check_output(['podman','inspect',name]))[0]
pid=o['State']['Pid'];fields=(pathlib.Path('/proc')/str(pid)/'stat').read_text().rsplit(')',1)[1].split()
print(json.dumps({'pid':pid,'cpu_seconds':(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK')}))'''
   p=both({h:'python3 -c '+shlex.quote(code.replace('NAME',repr(names[h]))) for h in HOSTS})
   return {h:json.loads(p[h].stdout) for h in HOSTS}
  for request,nt in enumerate((16,128,128)):
   label=f'cq-serve-r{run}-b{budget}-q{request}'
   a=cpu();p=ssh('fuzzy',f'python3 {LAB}/http-case.py {label} {nt} {words}');b=cpu()
   row=json.loads(p.stdout);row.update(run=run,cq_spin_us=budget,warmup=request==0)
   row['process_cpu_seconds']={h:b[h]['cpu_seconds']-a[h]['cpu_seconds'] for h in HOSTS}
   if request==0:words-=row['usage']['prompt_tokens']-2048
   else:assert row['timings']['prompt_n']==2048 and row['timings']['cache_n']==0
   assert all(a[h]['pid']==b[h]['pid'] for h in HOSTS)
   allrows.append(row);(ROOT/'cq-serve-summary.json').write_text(json.dumps(allrows,indent=2)+'\n')
   print(label,'prompt_n',row['timings']['prompt_n'],'cache_n',row['timings']['cache_n'],'tg128',row['timings']['predicted_per_second'],'cpu',row['process_cpu_seconds'],flush=True)
 finally:
  for h in HOSTS:
   ssh(h,f'podman logs {names[h]} > {LAB}/private/serve-r{run}-b{budget}.log 2>&1',False)
   p=ssh(h,f'podman logs {names[h]} 2>&1 | rg "event=rdma_ready|event=request_completed|event=generation_complete"',False)
   (ROOT/f'{h}-serve-r{run}-b{budget}-events.log').write_text(p.stdout)
   ssh(h,f'podman stop --time 30 {names[h]}',False)
   ssh(h,f'podman rm {names[h]}',False)
