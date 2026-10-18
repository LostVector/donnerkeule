#!/bin/bash
# linkarmed.sh NAME HOLDRANK UNBINDHOST: unbind UNBINDHOST's domain-1 services while HOLDRANK holds an armed credit
S=$(cd "$(dirname "$0")" && pwd); out=$S/runs
name=$1; hr=$2; uh=$3
D=/sys/bus/thunderbolt/drivers/thunderbolt_ibverbs
for h in fuzzy misty; do $S/mark.sh $h > $out/.mark-$h; ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-before; done
rm -f $out/fuzzy-$name.log $out/misty-$name.log
"$S/lab.sh" run $name --op SEND_IMM --bytes 10304 --depth 1 --iterations 8 --rounds 1 --hold-ms 20000 --hold-rank $hr > $out/$name.case 2>&1 &
bg=$!
for i in $(seq 1 60); do grep -q "^HOLD peer sent" $out/fuzzy-$name.log $out/misty-$name.log 2>/dev/null && break; sleep 1; done
sleep 3
echo "t=$(date +%T) unbinding $uh 1-2.0 / 1-2.1 (credit armed)"
ssh $uh "echo 1-2.0 | sudo tee $D/unbind >/dev/null; echo 1-2.1 | sudo tee $D/unbind >/dev/null"
sleep 6
echo "t=$(date +%T) rebinding"
ssh $uh "echo 1-2.0 | sudo tee $D/bind >/dev/null; echo 1-2.1 | sudo tee $D/bind >/dev/null"
wait $bg
grep -v credit_max $out/$name.case | cut -c1-200
sleep 2
for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > $out/$h-$name.peers-after; $S/since.sh $h $(cat $out/.mark-$h) > $out/$h-$name.dmesg; echo "-- $h dmesg"; cut -c1-200 $out/$h-$name.dmesg | grep -v "ib_mad\|open port\|MAD agent\|^\s*$" | head -14; done
python3 $S/delta.py $name $out
$S/gp.sh | tr '\n' ' '; echo; $S/nhi.sh
