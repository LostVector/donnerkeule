#!/usr/bin/env bash
# cohort.sh TAG MODE CREDITS [HOSTMR]: SEND_IMM and WRITE_IMM persistent cases.
set -uo pipefail
S=$(cd "$(dirname "$0")" && pwd)
tag=$1 mode=$2 credits=$3 host=${4:-}
$S/lab.sh load "$mode" "$credits" || exit 1
for op in SEND_IMM WRITE_IMM; do
	for spec in "32 1 128" "10304 1 128" "1048576 3 32" "20971584 3 16" "33554368 3 8"; do
		set -- $spec
		$S/lab.sh run "$tag-$op-$1" --op $op --bytes $1 --depth $2 --iterations $3 \
			--rounds 4 --persistent-rq --spin-us 5000 $host | head -1
	done
done
