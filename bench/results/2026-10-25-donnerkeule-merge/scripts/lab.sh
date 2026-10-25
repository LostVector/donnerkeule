#!/usr/bin/env bash
# Orchestrate fuzzy (rank 0) and misty (rank 1) from the Mac.
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
out=${OUT:-$S/runs}
mkdir -p "$out"
load() { # load base|test [params...]: Gufo must be stopped
	local h
	for h in fuzzy misty; do ssh "$h" "sudo bash ~/dk-lab/load.sh $*" & done; wait
	sleep 12
	for h in fuzzy misty; do
		echo "$h ready=$(ssh "$h" 'sudo grep -c "descriptor_ready=1" /sys/kernel/debug/thunderbolt_ibverbs/peers 2>/dev/null; sudo grep -c "push_done:.*ready=1" /sys/kernel/debug/thunderbolt_ibverbs/peers' | tr '\n' ' ')"
	done
}
snap() { for h in fuzzy misty; do ssh "$h" 'sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary' > "$out/$h-$1.$2"; done; }
VPOD='podman run --rm --security-opt label=disable --userns keep-id:uid=1000,gid=1000 --device /dev/kfd --device /dev/dri --device /dev/infiniband --group-add keep-groups --ulimit memlock=-1 --network host -v /home/sn/dk-lab:/lab -v /home/sn/tbv/c50:/home/sn/tbv/c50:ro -e RDMAV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma -e IBV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma --entrypoint /lab/bin/tbv_gpu_verbs localhost/gufo-tp2-dev:7.2.3'
PPOD='podman run --rm --security-opt label=disable --userns keep-id:uid=1000,gid=1000 --device /dev/kfd --device /dev/dri --device /dev/infiniband --group-add keep-groups --ulimit memlock=-1 --network host -v /home/sn/gufo-sendimm-lab/out:/out:ro -v /home/sn/tbv/c50:/home/sn/tbv/c50:ro -e RDMAV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma --entrypoint /out/probe localhost/gufo-tp2-dev:7.2.3'
run() { # run NAME verifier-args...
	local name=$1; shift
	snap "$name" before
	ssh fuzzy "timeout 900 $VPOD --device usb4_rdma0 --gid-index 1 $*" > "$out/fuzzy-$name.log" 2>&1 &
	local p0=$!; sleep 3
	ssh misty "timeout 900 $VPOD --device usb4_rdma0 --gid-index 1 --host 192.168.211.2 $*" > "$out/misty-$name.log" 2>&1
	local r1=$?; wait $p0; local r0=$?
	snap "$name" after
	echo "CASE $name rank0=$r0 rank1=$r1"; tail -1 "$out/fuzzy-$name.log"; tail -1 "$out/misty-$name.log"
}
probe() { # probe NAME probe-args...
	local name=$1; shift
	snap "$name" before
	ssh fuzzy "timeout 900 $PPOD --tp-rank 0 --tp-rdma-device usb4_rdma0 $*" > "$out/fuzzy-$name.log" 2>&1 &
	local p0=$!; sleep 3
	ssh misty "timeout 900 $PPOD --tp-rank 1 --tp-rdma-device usb4_rdma0 --tp-bootstrap-host 192.168.211.2 $*" > "$out/misty-$name.log" 2>&1
	local r1=$?; wait $p0; local r0=$?
	snap "$name" after
	echo "CASE $name rank0=$r0 rank1=$r1"
	grep -h -E "allreduce|mismatch|failed" "$out/fuzzy-$name.log" "$out/misty-$name.log" | cut -c1-220
}
wbw() { # wbw NAME [perftest args]: host-memory ib_write_bw, misty -> fuzzy
	local name=$1; shift
	local a="-d usb4_rdma0 -i 1 -x 1 -p 19123 -I 0 -F --report_gbits -q 1 $*"
	snap "$name" before
	ssh fuzzy "timeout 60 taskset -c 2 ib_write_bw $a" > "$out/fuzzy-$name.log" 2>&1 &
	local p0=$!; sleep 2
	ssh misty "timeout 60 taskset -c 2 ib_write_bw $a 192.168.211.2" > "$out/misty-$name.log" 2>&1
	local r1=$?; wait $p0; local r0=$?
	snap "$name" after
	echo "CASE $name rank0=$r0 rank1=$r1 $(grep -E '^ *[0-9]+ +[0-9]+' "$out/misty-$name.log" | tail -1)"
}
"$@"
