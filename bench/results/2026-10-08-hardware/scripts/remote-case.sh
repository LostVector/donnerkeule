#!/usr/bin/env bash
set -euo pipefail
lab=/home/sn/tbv/hardware-20261007-xdomain-done
case_name=${1:?case name}; shift
[[ $case_name =~ ^[a-z0-9_-]+$ ]]
test ! -e "$lab/results/$case_name.exit"
sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/summary > "$lab/results/$case_name.before-summary"
sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/peers > "$lab/results/$case_name.before-peers"
cat /proc/stat > "$lab/results/$case_name.before-cpu"
set +e
sudo -n timeout --signal=TERM --kill-after=10s 240s /usr/bin/time -o "$lab/results/$case_name.time" -f 'elapsed=%e user=%U system=%S cpu=%P maxrss=%M exit=%x' "$lab/run.sh" "$lab" "$@" > "$lab/results/$case_name.log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" > "$lab/results/$case_name.exit"
cat /proc/stat > "$lab/results/$case_name.after-cpu"
sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/summary > "$lab/results/$case_name.after-summary"
sudo -n cat /sys/kernel/debug/thunderbolt_ibverbs/peers > "$lab/results/$case_name.after-peers"
tail -4 "$lab/results/$case_name.log"
printf 'CASE %s exit=%s\n' "$case_name" "$rc"
exit "$rc"
