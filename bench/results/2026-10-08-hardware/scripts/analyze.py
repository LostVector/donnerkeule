#!/usr/bin/env python3
import tarfile,pathlib,re,statistics,json
root=pathlib.Path('/private/tmp/tbv-hardware-20261007')
for host in ('fuzzy','misty'):
 p=root/host; p.mkdir(exist_ok=True)
 with tarfile.open(root/(host+'-results.tar.gz')) as tf:
  for member in tf.getmembers():
   assert (p/member.name).resolve().is_relative_to(p.resolve()) and not member.issym() and not member.islnk()
  tf.extractall(p)
rows=[]
for size in (10304,1048576,20971584):
 row={'bytes':size}
 for mode in ('status','done'):
  samples=[];cpu=[];runs=[];fallback=0;donecalls=0
  for run in range(4):
   name=f'ab{run}-{mode}-{size}';rr=[];c=0
   for host in ('fuzzy','misty'):
    p=root/host/(name+'.log')
    if not p.exists():continue
    for l in p.read_text().splitlines():
     if l.startswith('RESULT ') and 'round=0 ' not in l:
      rr.append(float(re.search(r'us_per_message=([0-9.]+)',l)[1]))
    tt=(root/host/(name+'.time')).read_text()
    mm=re.search(r'user=([\d.]+) system=([\d.]+)',tt);c+=float(mm[1])+float(mm[2])
    def stats(s):return {k:int(v) for k,v in re.findall(r'^(dmabuf_\w+): (\d+)$',s,re.M)}
    a=stats((root/host/(name+'.after-summary')).read_text());b=stats((root/host/(name+'.before-summary')).read_text())
    fallback+=a.get('dmabuf_status_calls',0)-b.get('dmabuf_status_calls',0)
    donecalls+=a.get('dmabuf_done_wait_calls',0)-b.get('dmabuf_done_wait_calls',0)
   if rr:samples+=rr;cpu.append(c);runs.append(statistics.median(rr))
  row[mode]={'median_us':statistics.median(samples),'run_medians_us':runs,'total_cpu_seconds_per_pair':cpu,'status_calls':fallback,'done_wait_calls':donecalls}
 row['latency_change_pct']=(row['done']['median_us']/row['status']['median_us']-1)*100
 rows.append(row)
print(json.dumps(rows,indent=2))
(root/'ab-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
