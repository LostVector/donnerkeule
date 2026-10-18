#!/usr/bin/env bash
# loss.sh case NAME "fuzzy:credit drop count=1" "misty:done drop" -- verifier-args
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
out=$S/runs
P=/sys/module/thunderbolt_ibverbs/parameters/dmabuf_fault
setfault() { ssh "$1" "echo '$2' | sudo tee $P >/dev/null" ; }
dm() { ssh "$1" 'cut -d" " -f1 /proc/uptime'; }
case_() {
	local name=$1; shift
	local rules=()
	while [[ $1 != -- ]]; do rules+=("$1"); shift; done
	shift
	local h
	for h in fuzzy misty; do /private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/mark.sh $h > $out/.mark-$h; setfault $h clear; done
	for r in ${rules[@]+"${rules[@]}"}; do setfault "${r%%:*}" "${r#*:}" || echo "setfault failed $r"; done
	for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > "$out/$h-$name.peers-before"; done
	local t0=$(date +%s)
	"$S/lab.sh" ${LABCMD:-run} "$name" "$@" 2>&1 | tee "$out/$name.case"
	grep -H "FAIL" "$out"/fuzzy-$name.log "$out"/misty-$name.log | tee -a "$out/$name.case"
	sleep 2
	echo "elapsed=$(( $(date +%s) - t0 ))s" | tee -a "$out/$name.case"
	for h in fuzzy misty; do ssh $h "sudo cat /sys/kernel/debug/thunderbolt_ibverbs/peers" > "$out/$h-$name.peers-after"; done
	python3 "$S/delta.py" "$name" "$out" | tee -a "$out/$name.case"
	for h in fuzzy misty; do
		ssh $h "sudo cat $P" > "$out/$h-$name.faults"
		/private/tmp/claude-502/-Users-user-Documents-thunderbolt-ibverbs/36783288-9d76-4864-a850-ee2d089e4d76/scratchpad/since.sh $h $(cat $out/.mark-$h) > "$out/$h-$name.dmesg"
		setfault $h clear
		echo "--- $h dmesg ($(wc -l < "$out/$h-$name.dmesg") lines)"; cut -c1-200 "$out/$h-$name.dmesg" | head -15
		grep -h "hits=[1-9]" "$out/$h-$name.faults" | sed "s/^/$h faults: /"
	done
}
state() { # summary of quarantine/health
	for h in fuzzy misty; do
		echo "== $h"; ssh $h 'sudo grep -E "stop|quarant|credit_(mismatch|aborted)|notify_failed|rail.*(state|stopped)" /sys/kernel/debug/thunderbolt_ibverbs/summary /sys/kernel/debug/thunderbolt_ibverbs/peers | head -20'
	done
}
"$@"
