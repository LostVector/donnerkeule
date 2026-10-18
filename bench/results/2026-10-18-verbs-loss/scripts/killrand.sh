#!/bin/bash
# killrand.sh NAME KILLHOST DELAY_SEC [verifier args...]: SIGKILL a verifier mid-stream
S=$(cd "$(dirname "$0")" && pwd); out=$S/runs
name=$1; kh=$2; delay=$3; shift 3
args=${*:-"--op SEND_IMM --bytes 10304 --depth 1 --iterations 4000 --rounds 1 --persistent-rq --spin-us 5000"}
n=$("$S/gp.sh" | grep -c "stopped_rails=[1-9]"); [ "$n" != 0 ] && "$S/lab.sh" load dmabuf 1 >/dev/null 2>&1
for h in fuzzy misty; do /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/mark.sh $h > $out/.mark-$h; ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-before; done
rm -f $out/fuzzy-$name.log $out/misty-$name.log
"$S/lab.sh" run $name $args > $out/$name.case 2>&1 &
bg=$!
for i in $(seq 1 60); do grep -q "^BOOTSTRAP" $out/fuzzy-$name.log $out/misty-$name.log 2>/dev/null && break; sleep 0.5; done
sleep $delay
t0=$(date +%s.%N)
ssh $kh 'sudo pkill -9 -x tbv_gpu_verbs' >/dev/null 2>&1
wait $bg
echo "$name kill=$kh after=${delay}s; wait_after_kill=$(echo "$(date +%s.%N) - $t0" | bc)"
sleep 2
grep -h "FAIL\|^CASE" $out/$name.case $out/fuzzy-$name.log $out/misty-$name.log | sort -u | cut -c1-160
for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-after; /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/since.sh $h $(cat $out/.mark-$h) > $out/$h-$name.dmesg; cut -c1-200 $out/$h-$name.dmesg | head -5 | sed "s/^/$h dmesg: /"; done
python3 $S/delta.py $name $out
$S/gp.sh | tr '\n' ' '; echo
