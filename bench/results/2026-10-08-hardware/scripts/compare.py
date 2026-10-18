#!/usr/bin/env python3
from control import switch, pair, save_state
for run,push in enumerate((0,1,1,0)):
    mode='done' if push else 'status'
    switch('dmabuf',push,0,2)
    save_state(f'ab{run}-{mode}-initial')
    for size,depth,iterations in ((10304,1,256),(1048576,3,32),(20971584,3,16)):
        pair(f'ab{run}-{mode}-{size}', ['--op','WRITE_IMM','--bytes',str(size),'--depth',str(depth),'--iterations',str(iterations),'--rounds','4','--spin-us','5000','--persistent-rq'])
