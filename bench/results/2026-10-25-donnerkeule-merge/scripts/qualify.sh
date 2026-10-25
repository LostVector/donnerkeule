#!/usr/bin/env bash
# qualify.sh NAME params...: full correctness set on one profile
cd "$(dirname "$0")"
n=$1; shift
for h in fuzzy misty; do ssh $h 'sudo dmesg -C'; done
./lab.sh load test "$@"
ok=0; bad=0
c() { local r; r=$(./lab.sh run "$n-$1" "${@:2}" | head -1); [[ $r == *rank0=0\ rank1=0* ]] && ok=$((ok+1)) || { bad=$((bad+1)); echo "FAIL $r"; }; }
for op in SEND SEND_IMM WRITE_IMM WRITE; do c m-$op --op $op --iterations 8 --rounds 2 --depth 3; done
c remerr --op SEND --iterations 8 --rounds 1 --remote-error
c rnr --op SEND --iterations 8 --rounds 1 --rnr-delay-ms 100 --signal-all
for op in SEND SEND_IMM WRITE_IMM WRITE; do c host-$op --op $op --iterations 8 --rounds 2 --host-mr; done
c persist --op SEND_IMM --bytes 10304 --depth 1 --iterations 4000 --rounds 2 --persistent-rq --spin-us 5000
for i in 1 2 3; do ./lab.sh wbw $n-wbw$i -s 16777216 -D 3 -t 32 | awk '{print "wbw", $(NF-1)}'; done
./lab.sh wbw $n-wbw-small -s 65536 -D 3 -t 128 | awk '{print "wbw64k", $(NF-1)}'
echo "$n: $ok passed, $bad failed"
for h in fuzzy misty; do echo "$h: $(ssh $h 'sudo dmesg | grep -ciE "stopped after|unexpected|BUG|WARNING|Oops|release failed"') bad kernel lines; $(ssh $h 'sudo grep -c "descriptor_ready=1 quarantine_reason=0" /sys/kernel/debug/thunderbolt_ibverbs/peers') healthy rails; $(ssh $h 'sudo grep -E "credit_(mismatch|aborted|readopted)" /sys/kernel/debug/thunderbolt_ibverbs/summary' | tr '\n' ' ')"; done
