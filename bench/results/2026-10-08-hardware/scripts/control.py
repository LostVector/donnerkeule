#!/usr/bin/env python3
import concurrent.futures, json, pathlib, shlex, subprocess, sys, time
ROOT=pathlib.Path('/private/tmp/tbv-hardware-20261007')
LAB='/home/sn/tbv/hardware-20261007-xdomain-done'
HOSTS=('fuzzy','misty')
def ssh(host,command,check=True):
    p=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10','-o','HostName=192.168.211.'+('2' if host=='fuzzy' else '3'),'-o','HostKeyAlias='+host+'.fritz.box',host,command],capture_output=True,text=True)
    if check and p.returncode:
        raise RuntimeError(host+': '+p.stdout+p.stderr)
    return p
def both(commands):
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        fs={host:pool.submit(ssh,host,commands[host]) for host in HOSTS}
        results={}; errors=[]
        for host,f in fs.items():
            try: results[host]=f.result()
            except Exception as e: errors.append(str(e))
        if errors: raise RuntimeError('\n'.join(errors))
        return results
def save_state(label):
    command=f'sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/summary /sys/kernel/debug/thunderbolt_ibverbs/peers'
    results=both(dict.fromkeys(HOSTS,command))
    for host,p in results.items(): (ROOT/(host+'-'+label+'.txt')).write_text(p.stdout)
    return results
def switch(mode,push=0,ring=0,lanes=2,profile=1,mixed=False):
    both(dict.fromkeys(HOSTS,f'sudo -n {LAB}/module.sh {LAB} unload'))
    params=f'TBV_LAB_PROFILE={profile} TBV_LAB_CONTROL_RING={ring} TBV_LAB_LANES={lanes}'
    both({host:f'sudo -n env {params} TBV_LAB_PUSH_DONE={push if host=="fuzzy" or not mixed else 0} {LAB}/module.sh {LAB} load {mode}' for host in HOSTS})
    for _ in range(30):
        state=save_state('current')
        if all(p.stdout.count('data_ready=1')==2*lanes for p in state.values()):
            if ring and not all(p.stdout.count('control_ring: requested=1 remote=1 ready=1')==2 for p in state.values()):
                time.sleep(1); continue
            if push and not ring and not mixed and not all(p.stdout.count('push_done: requested=1 remote=1 ready=1')==2 for p in state.values()):
                time.sleep(1); continue
            print('READY',mode,'push',push,'ring',ring,'lanes',lanes,'mixed',mixed,flush=True)
            return
        time.sleep(1)
    raise RuntimeError('Peers not ready; latest state saved locally')
def pair(name,args,port=19300):
    state=both(dict.fromkeys(HOSTS,'ls /sys/class/infiniband'))
    devices={host:[x for x in p.stdout.split() if x.startswith('usb4_rdma')][0] for host,p in state.items()}
    base=['bash',LAB+'/remote-case.sh',name,'--port',str(port),'--gid-index','1','--device']
    commands={host:shlex.join(base+[devices[host]]+args+(['--host','192.168.211.2'] if host=='misty' else [])) for host in HOSTS}
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        a=pool.submit(ssh,'fuzzy',commands['fuzzy'],False)
        time.sleep(0.4)
        b=pool.submit(ssh,'misty',commands['misty'],False)
        results={'fuzzy':a.result(),'misty':b.result()}
    failed=[]
    for host,p in results.items():
        print(host,p.stdout,p.stderr,flush=True)
        (ROOT/(host+'-'+name+'-result.txt')).write_text(p.stdout+p.stderr)
        if p.returncode: failed.append(host)
    if failed: raise RuntimeError('Case failed: '+name+' '+','.join(failed))
if __name__=='__main__':
    if sys.argv[1]=='switch': switch(sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),int(sys.argv[5]),mixed='mixed' in sys.argv)
    elif sys.argv[1]=='pair': pair(sys.argv[2],sys.argv[3:])
    elif sys.argv[1]=='state': save_state(sys.argv[2])
