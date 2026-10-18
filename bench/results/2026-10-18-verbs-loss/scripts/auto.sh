#!/bin/bash
# auto.sh NAME WORKLOAD-KEY "rule" "rule" ... : reload if quarantined, run, compact summary
S=$(cd "$(dirname "$0")" && pwd)
name=$1; wl=$2; shift 2
case $wl in
 w1) args="--op SEND_IMM --bytes 10304 --depth 1 --iterations 128 --rounds 1 --persistent-rq --spin-us 5000";;
 w1b) args="--op SEND_IMM --bytes 4096 --depth 1 --iterations 64 --rounds 1 --persistent-rq --spin-us 5000";;
 m) args="--op SEND --iterations 8 --rounds 2";;
 mi) args="--op SEND_IMM --iterations 8 --rounds 2";;
 full) args="--iterations 8 --rounds 2 --remote-error";;
 *) args=$wl;;
esac
n=$("$S/gp.sh" | grep -c "stopped_rails=[1-9]")
if [ "$n" != 0 ]; then "$S/lab.sh" load dmabuf 1 >/dev/null 2>&1; fi
"$S/loss.sh" case_ "$name" "$@" -- $args 2>&1 | grep -v "credit_max\|^VERIFY\|^POST\|^PHASE\|^/private\|^--- \|dmesg)\|^HOLD"
"$S/gp.sh" | tr '\n' ' '; echo
