#!/usr/bin/env bash
# gsum.sh PREFIX MODE...: p50/p99 per rows for runs PREFIX-MODE-N
S=$(cd "$(dirname "$0")" && pwd)/gufo
prefix=$1; shift
for m in "$@"; do for f in $S/{fuzzy,misty}-$prefix-$m-*.log; do
	grep -h "allreduce rows" "$f" | sed -E "s/.*rows=([0-9]+).*p50_us=([0-9.]+) mean_us=([0-9.]+) p99_us=([0-9.]+) verified=([0-9])/$m \1 \2 \4 \5/"
done; done | awk '{k=$1" rows="$2; p50[k]=p50[k]" "$3; p99[k]=p99[k]" "$4; v[k]+=$5; n[k]++} END{for (k in p50) print k, "p50:"p50[k], " p99:"p99[k], " verified="v[k]"/"n[k]}' | sort -k1,1 -k2,2V
