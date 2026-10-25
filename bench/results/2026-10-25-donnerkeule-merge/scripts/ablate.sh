#!/usr/bin/env bash
# ablate.sh NAME params...: load, then host bandwidth, small-SEND latency and Gufo probe
cd "$(dirname "$0")"
n=$1; shift
./lab.sh load test "$@" >/dev/null
w=$(./lab.sh wbw a-$n-wbw -s 16777216 -D 3 -t 32 | awk '{print $(NF-1)}')
./lab.sh run a-$n-lat --op SEND_IMM --bytes 10304 --depth 1 --iterations 2000 --rounds 1 --persistent-rq --spin-us 5000 >/dev/null
l=$(grep -h "^RESULT" runs/fuzzy-a-$n-lat.log runs/misty-a-$n-lat.log | grep -oE "us_per_message=[0-9.]+" | cut -d= -f2 | tr '\n' '/')
./lab.sh probe a-$n-probe --allreduce-bench 1000 >/dev/null
p=$(grep -h "allreduce rows" runs/fuzzy-a-$n-probe.log runs/misty-a-$n-probe.log | sed -E 's/.*rows=([0-9]+).*p50_us=([0-9.]+).*p99_us=([0-9.]+).*verified=([0-9]).*/\1:\2\/\3:\4/' | tr '\n' ' ')
echo "$n wbw=${w}Gb/s sendimm10k=${l} probe(rows:p50/p99:ok)= $p"
