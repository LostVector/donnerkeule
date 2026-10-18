from control import both, ssh, HOSTS, LAB, ROOT
import concurrent.futures,json,pathlib,shlex,subprocess
code='''import json,pathlib,re,subprocess
lab=pathlib.Path(LAB)
cursor=next(line.removeprefix('-- cursor: ').strip() for line in (lab/'private/kernel-cursor.txt').read_text().splitlines() if line.startswith('-- cursor: '))
assert cursor.startswith('s='), 'Unexpected journal cursor'
raw=subprocess.check_output(['journalctl','-k','--after-cursor='+cursor,'--no-pager','-o','short-iso'],text=True)
(lab/'results/kernel-final.log').write_text(raw)
pattern=r'WARNING:|BUG:|Oops:|KASAN:|UBSAN:|general protection fault|IOMMU.*fault|AMD-Vi.*(?:fault|error)|amdgpu.*(?:reset|timeout|fault)|Out of memory|oom-kill'
hits=[line for line in raw.splitlines() if re.search(pattern,line,re.IGNORECASE)]
summary={'kernel_error_marker_count':len(hits),'kernel_error_markers':hits,
 'taint':int(pathlib.Path('/proc/sys/kernel/tainted').read_text())}
(lab/'results/kernel-final-summary.json').write_text(json.dumps(summary,indent=2)+'\\n')
for name in ('summary','peers'):
 (lab/('results/restored-'+name+'.txt')).write_bytes((pathlib.Path('/sys/kernel/debug/thunderbolt_ibverbs')/name).read_bytes())
print(json.dumps(summary))
'''.replace('LAB',repr(LAB))
for h,p in both(dict.fromkeys(HOSTS,'sudo -n python3 -c '+shlex.quote(code))).items():
    print(h,p.stdout,flush=True)
    (ROOT/(h+'-kernel-final-summary.json')).write_text(p.stdout)

def collect(host):
    args=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=10','-o',
        'HostName=192.168.211.'+('2' if host=='fuzzy' else '3'),'-o',
        'HostKeyAlias='+host+'.fritz.box',host,
        f'sudo -n tar -C {LAB}/results -czf - .']
    with (ROOT/(host+'-results-final.tar.gz')).open('wb') as out:
        subprocess.run(args,stdout=out,check=True)
    print(host,'public results collected',flush=True)
with concurrent.futures.ThreadPoolExecutor(2) as pool:
    futures=[pool.submit(collect,h) for h in HOSTS]
    for f in futures:f.result()
