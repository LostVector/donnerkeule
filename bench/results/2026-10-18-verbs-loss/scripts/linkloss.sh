#!/bin/bash
# linkloss.sh NAME: unbind the thunderbolt_ibverbs services of fuzzy's domain 1 mid-stream
S=$(cd "$(dirname "$0")" && pwd); out=$S/runs
name=$1
D=/sys/bus/thunderbolt/drivers/thunderbolt_ibverbs
for h in fuzzy misty; do $S/mark.sh $h > $out/.mark-$h; ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-before; done
$S/nhi.sh > $out/$name.nhi-before; cat $out/$name.nhi-before
rm -f $out/fuzzy-$name.log $out/misty-$name.log
"$S/lab.sh" run $name --op SEND_IMM --bytes 10304 --depth 1 --iterations 10000 --rounds 20 --persistent-rq --spin-us 5000 > $out/$name.case 2>&1 &
bg=$!
for i in $(seq 1 60); do grep -q "^BOOTSTRAP" $out/fuzzy-$name.log $out/misty-$name.log 2>/dev/null && break; sleep 0.5; done
sleep 3
echo "t=$(date +%T) unbinding fuzzy 1-2.0 / 1-2.1"
ssh fuzzy "echo 1-2.0 | sudo tee $D/unbind >/dev/null; echo 1-2.1 | sudo tee $D/unbind >/dev/null" ; echo "unbind rc=$?"
t0=$(date +%s)
wait $bg
echo "t=$(date +%T) run ended ${i}; $(( $(date +%s) - t0 ))s after unbind"
grep -h "FAIL\|^CASE" $out/$name.case | cut -c1-200
sleep 2
for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-after; $S/since.sh $h $(cat $out/.mark-$h) > $out/$h-$name.dmesg; echo "-- $h dmesg ($(wc -l < $out/$h-$name.dmesg) lines)"; cut -c1-220 $out/$h-$name.dmesg | grep -v "^\s*$" | head -25; done
python3 $S/delta.py $name $out
$S/nhi.sh
