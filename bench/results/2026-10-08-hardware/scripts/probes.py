from control import both,HOSTS,LAB,ssh,ROOT
import json, pathlib,time
for budget in (5000,50,0):
 names={host:f'tbv-cq-probe-{i}-{budget}' for i,host in enumerate(HOSTS)}
 for host in HOSTS:print(ssh(host,f'python3 {LAB}/gufo-launch.py probe {budget}').stdout,flush=True)
 deadline=time.monotonic()+240
 while time.monotonic()<deadline:
  state=both({host:f'podman inspect {names[host]} --format "{{{{.State.Running}}}} {{{{.State.ExitCode}}}}"' for host in HOSTS})
  if all(p.stdout.startswith('false ') for p in state.values()):break
  time.sleep(2)
 else: raise RuntimeError('CQ probe timeout')
 for host in HOSTS:
  out=ssh(host,f'podman logs {names[host]}')
  (ROOT/f'{host}-cq-probe-{budget}.log').write_text(out.stdout+out.stderr)
  ssh(host,f'podman logs {names[host]} > {LAB}/results/cq-probe-{budget}.log 2>&1')
  print(host,'budget',budget,state[host].stdout.strip(),(out.stdout+out.stderr)[-1000:],flush=True)
  assert state[host].stdout.strip()=='false 0'
 for host in HOSTS:ssh(host,f'podman rm {names[host]}')
