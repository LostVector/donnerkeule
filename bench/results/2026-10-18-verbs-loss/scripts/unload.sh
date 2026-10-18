#!/bin/bash
# unload.sh NAME HOLDRANK RELOADHOST [unload-only-seconds]: reload RELOADHOST's module while HOLDRANK holds an armed credit
S=$(cd "$(dirname "$0")" && pwd); out=$S/runs
name=$1; hr=$2; rh=$3; gap=${4:-2}
n=$("$S/gp.sh" | grep -c "stopped_rails=[1-9]"); [ "$n" != 0 ] && "$S/lab.sh" load dmabuf 1 >/dev/null 2>&1
rm -f $out/fuzzy-$name.log $out/misty-$name.log
for h in fuzzy misty; do /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/mark.sh $h > $out/.mark-$h; ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-before; done
"$S/lab.sh" run $name --op SEND_IMM --bytes 10304 --depth 1 --iterations 8 --rounds 1 --hold-ms 25000 --hold-rank $hr > $out/$name.case 2>&1 &
bg=$!
for i in $(seq 1 60); do grep -q "^HOLD peer sent" $out/fuzzy-$name.log $out/misty-$name.log 2>/dev/null && break; sleep 1; done
sleep 3
other=$([ $rh = fuzzy ] && echo misty || echo fuzzy)
[ -n "$KILL" ] && { ssh $KILL "sudo pkill -9 -x tbv_gpu_verbs" >/dev/null 2>&1; sleep 0.5; echo "killed verifier on $KILL"; }
echo "t=$(date +%T) unloading $rh"
ssh $rh 'sudo bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab unload' && echo "unloaded"
sleep $gap
echo "t=$(date +%T) loading $rh"
ssh $rh "sudo TBV_LAB_PUSH_DONE=1 TBV_LAB_LANES=2 TBV_LAB_CREDITS=1 bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab load dmabuf" && echo "loaded"
for i in $(seq 1 30); do c=$(ssh $other 'sudo grep -c "push_done:.*ready=1" /sys/kernel/debug/thunderbolt_ibverbs/peers'); [ "$c" = 2 ] && break; sleep 1; done
echo "t=$(date +%T) peers ready on $other: $c (waited ${i}s)"
echo "-- $other peers after reload:"; ssh $other 'sudo grep -E "^peer|push_done:" /sys/kernel/debug/thunderbolt_ibverbs/peers'
wait $bg
grep -v credit_max $out/$name.case | cut -c1-200
sleep 1
for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-after; /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/since.sh $h $(cat $out/.mark-$h) > $out/$h-$name.dmesg; echo "-- $h dmesg"; cut -c1-220 $out/$h-$name.dmesg | grep -v "^\s*$" | head -12; done
python3 $S/delta.py $name $out
$S/gp.sh | tr '\n' ' '; echo
