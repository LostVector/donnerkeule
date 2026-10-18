#!/bin/bash
# kill.sh NAME KILLHOST HOLDRANK : hold an armed credit on HOLDRANK, SIGKILL the verifier on KILLHOST during the hold
S=$(cd "$(dirname "$0")" && pwd); out=$S/runs
name=$1; kh=$2; hr=$3
n=$("$S/gp.sh" | grep -c "stopped_rails=[1-9]"); [ "$n" != 0 ] && "$S/lab.sh" load dmabuf 1 >/dev/null 2>&1
for h in fuzzy misty; do /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/mark.sh $h > $out/.mark-$h; ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-before; done
rm -f $out/fuzzy-$name.log $out/misty-$name.log
"$S/lab.sh" run $name --op SEND_IMM --bytes 10304 --depth 1 --iterations 8 --rounds 1 --hold-ms 20000 --hold-rank $hr > $out/$name.case 2>&1 &
bg=$!
for i in $(seq 1 60); do grep -q "^HOLD peer sent" $out/fuzzy-$name.log $out/misty-$name.log 2>/dev/null && break; sleep 1; done
sleep 3
t0=$(date +%s.%N)
ssh $kh 'sudo pkill -9 -x tbv_gpu_verbs; sleep 0.2; pgrep -fa tbv_gpu_verbs | grep -v pgrep | head -3; echo killed'
wait $bg
echo "elapsed_after_kill=$(echo "$(date +%s.%N) - $t0" | bc)"
sleep 2
cat $out/$name.case | grep -v "credit_max"
for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-after; /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/since.sh $h $(cat $out/.mark-$h) > $out/$h-$name.dmesg; echo "-- $h dmesg"; cut -c1-200 $out/$h-$name.dmesg | head; done
python3 $S/delta.py $name $out
$S/gp.sh | tr '\n' ' '; echo
