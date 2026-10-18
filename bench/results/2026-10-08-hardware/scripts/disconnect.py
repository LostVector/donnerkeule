from control import pair,ssh,save_state,ROOT,LAB
import concurrent.futures,json,re,shlex,time
case='disconnect-other-link'
with concurrent.futures.ThreadPoolExecutor(1) as pool:
 future=pool.submit(pair,case,['--op','WRITE_IMM','--bytes','20971584','--iterations','256','--rounds','1','--spin-us','5000','--persistent-rq'])
 for _ in range(100):
  p=ssh('fuzzy','sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/peers')
  blocks=re.split(r'(?=^peer \d+ )',p.stdout,flags=re.M)
  active=[b for b in blocks if re.search(r'^  control_tx: ([1-9]\d*)$',b,re.M)]
  if active and sum(int(v) for v in re.findall(r'^  control_done: (\d+)$',p.stdout,re.M))>16:
   assert len(active)==1
   domain=int(re.search(r'\bdomain=(\d+)',active[0])[1])
   other=1-domain
   names=[f'{other}-2.0',f'{other}-2.1']
   assert all(ssh('fuzzy',f'test -L /sys/bus/thunderbolt/drivers/thunderbolt_ibverbs/{n}',False).returncode==0 for n in names)
   save_state(case+'-before-unbind')
   for n in names:
    ssh('fuzzy',f"printf '%s' {shlex.quote(n)} | sudo -n timeout 30 tee /sys/bus/thunderbolt/drivers/thunderbolt_ibverbs/unbind >/dev/null")
   (ROOT/(case+'.json')).write_text(json.dumps({'control_domain':domain,'unbound_service_domain':other,'services':names,'method':'unbind both driver services on the other physical link during sustained DMA; management remains on Ethernet'},indent=2)+'\n')
   print('Unbound other link services:',names,flush=True)
   break
  if future.done(): raise RuntimeError('Traffic finished before disconnect injection')
  time.sleep(.1)
 else: raise RuntimeError('No active transaction found for disconnect injection')
 try:
  future.result()
  print('Transfer survived link removal with verified payloads',flush=True)
 except RuntimeError as e:
  print('Expected disconnect result:',e,flush=True)
 for n in names:
  ssh('fuzzy',f"printf '%s' {shlex.quote(n)} | sudo -n timeout 30 tee /sys/bus/thunderbolt/drivers/thunderbolt_ibverbs/bind >/dev/null")
 save_state(case+'-after-rebind')
