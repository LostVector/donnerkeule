from control import switch, pair, both, HOSTS
switch('dmabuf',1,0,2)
p=both(dict.fromkeys(HOSTS,'sudo -n cat /sys/module/thunderbolt_ibverbs/parameters/lab_done_drop_every'))
assert all(r.stdout.strip()=='3' for r in p.values())
pair('drop-done-gpu-v2',['--op','WRITE_IMM','--bytes','1048576','--iterations','16','--rounds','2','--spin-us','5000','--persistent-rq'])
pair('drop-done-host-v2',['--host-mr','--bytes','10304','--iterations','8','--rounds','2','--spin-us','0','--signal-all'])
