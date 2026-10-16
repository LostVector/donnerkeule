#!/usr/bin/env bash
# Orchestrate fuzzy (rank 0) and misty (rank 1) from this machine.
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
out=$S/runs
load() { # load normal|dmabuf CREDITS
	local mode=$1 credits=$2 h
	for h in fuzzy misty; do
		ssh "$h" 'sudo bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab unload' || return 1
	done
	for h in fuzzy misty; do
		ssh "$h" "sudo TBV_LAB_DYNDBG=${TBV_LAB_DYNDBG:-} TBV_LAB_DIRECT_POST=${TBV_LAB_DIRECT_POST:-} TBV_LAB_PUSH_DONE=1 TBV_LAB_LANES=2 TBV_LAB_CREDITS=$credits bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab load $mode" || return 1
	done
	sleep 10
	for h in fuzzy misty; do
		ssh "$h" 'sudo grep -E "dmabuf_(verbs|push_done|credits):" /sys/kernel/debug/thunderbolt_ibverbs/summary; sudo grep -c "push_done:.*ready=1" /sys/kernel/debug/thunderbolt_ibverbs/peers || true'
	done
}
rebuild() {
	local h
	for h in fuzzy misty; do
		rsync -a --delete --exclude .git --exclude bench/results /Users/user/Documents/thunderbolt-ibverbs/ "${h}:tbv-credits-lab/src/" || return 1
	done
	for h in fuzzy misty; do
		ssh "$h" 'bash ~/tbv-credits-lab/build.sh && cp ~/tbv-credits-lab/test-signed.ko ~/tbv-credits-lab/lab/test-signed.ko' &
	done
	wait
}
trace_on() {
	for h in fuzzy misty; do ssh "$h" 'sudo sh -c "echo 0 > /sys/kernel/tracing/tracing_on; echo 16384 > /sys/kernel/tracing/buffer_size_kb; echo > /sys/kernel/tracing/trace; echo 1 > /sys/kernel/tracing/events/thunderbolt_ibverbs/tbv_perf/enable; echo 1 > /sys/kernel/tracing/tracing_on"'; done
}
trace_off() { # trace_off NAME
	for h in fuzzy misty; do ssh "$h" "sudo sh -c 'echo 0 > /sys/kernel/tracing/tracing_on; cat /sys/kernel/tracing/trace; echo 0 > /sys/kernel/tracing/events/thunderbolt_ibverbs/tbv_perf/enable'" > "$out/$h-$1.trace"; done
}
restore() {
	for h in fuzzy misty; do ssh "$h" 'sudo bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab unload'; done
	for h in fuzzy misty; do ssh "$h" 'sudo bash ~/tbv-credits-lab/switch.sh ~/tbv-credits-lab/lab restore && echo restored'; done
}
run() { # run NAME verifier-args...
	local name=$1; shift
	local podman='podman run --rm --security-opt label=disable --userns keep-id:uid=1000,gid=1000 --device /dev/kfd --device /dev/dri --device /dev/infiniband --group-add keep-groups --ulimit memlock=-1 --network host -v /home/sn/tbv-credits-lab:/lab -v /home/sn/tbv/c50:/home/sn/tbv/c50:ro -e RDMAV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma -e IBV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma --entrypoint /lab/bin/tbv_gpu_verbs localhost/gufo-tp2-dev:7.2.3'
	for h in fuzzy misty; do ssh "$h" 'sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary' > "$out/$h-$name.before" ; done
	ssh fuzzy "timeout 900 $podman --device usb4_rdma0 --gid-index 1 $*" > "$out/fuzzy-$name.log" 2>&1 &
	local p0=$!
	sleep 3
	ssh misty "timeout 900 $podman --device usb4_rdma0 --gid-index 1 --host 192.168.211.2 $*" > "$out/misty-$name.log" 2>&1
	local r1=$?
	wait $p0; local r0=$?
	for h in fuzzy misty; do ssh "$h" 'sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary' > "$out/$h-$name.after"; done
	echo "CASE $name rank0=$r0 rank1=$r1"
	tail -2 "$out/fuzzy-$name.log"; tail -2 "$out/misty-$name.log"
	grep -E "credit_" "$out/fuzzy-$name.after" | tr '\n' ' '; echo
	grep -E "credit_" "$out/misty-$name.after" | tr '\n' ' '; echo
}
pinned() { # pinned PODMAN-CMD CPUS: run the probe under taskset when CPUS is set
	if [[ -n $2 ]]; then
		printf '%s -c %s /out/probe' "${1/--entrypoint \/out\/probe/--entrypoint taskset}" "$2"
	else
		printf '%s' "$1"
	fi
}
gufo() { # gufo NAME probe-args...: Gufo TP probe, rank 0 on fuzzy
	local name=$1; shift
	local podman='podman run --rm --security-opt label=disable --userns keep-id:uid=1000,gid=1000 --device /dev/kfd --device /dev/dri --device /dev/infiniband --group-add keep-groups --ulimit memlock=-1 --network host -v /home/sn/gufo-sendimm-lab/out:/out:ro -v /home/sn/tbv/c50:/home/sn/tbv/c50:ro -e RDMAV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma --entrypoint /out/probe localhost/gufo-tp2-dev:7.2.3'
	for h in fuzzy misty; do ssh "$h" 'sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary' > "$out/$h-$name.before"; done
	ssh fuzzy "timeout 900 $(pinned "$podman" "${CPUSET0:-}") --tp-rank 0 --tp-rdma-device usb4_rdma0 $*" > "$out/fuzzy-$name.log" 2>&1 &
	local p0=$!
	sleep 3
	ssh misty "timeout 900 $(pinned "$podman" "${CPUSET1:-}") --tp-rank 1 --tp-rdma-device usb4_rdma0 --tp-bootstrap-host 192.168.211.2 $*" > "$out/misty-$name.log" 2>&1
	local r1=$?
	wait $p0; local r0=$?
	for h in fuzzy misty; do ssh "$h" 'sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary' > "$out/$h-$name.after"; done
	echo "CASE $name rank0=$r0 rank1=$r1"
	grep -h -E "transport|allreduce|mismatch|failed" "$out/fuzzy-$name.log" "$out/misty-$name.log" | cut -c1-200
}
"$@"
