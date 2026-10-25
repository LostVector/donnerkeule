#!/usr/bin/env bash
# gufo.sh NAME q4|q8 [reps]: start the Gufo TP=2 service, run the serving bench, stop it.
cd "$(dirname "$0")"
n=$1 m=$2 reps=${3:-5}
t0=$(date +%s)
ssh fuzzy "systemctl --user start gufo@$m" || exit 1
for i in $(seq 1 120); do
	ssh fuzzy 'curl -sf -m 5 http://127.0.0.1:8000/v1/models >/dev/null' && break
	sleep 10
done
echo "$n: ready after $(( $(date +%s) - t0 ))s"
ssh fuzzy "cd ~/git/gufo && timeout 2400 python3 tools/serving/gufo-serving-bench.py --fingerprint ~/dk-lab/fp.json --base-url http://127.0.0.1:8000 --model qwen3.8-flash-next --max-tokens 256 --warmup 1 --repetitions $reps --temperature 0 --note '$n' --output ~/dk-lab/gufo-$n.json" > runs/gufo-$n.txt 2>&1
tail -5 runs/gufo-$n.txt
ssh fuzzy "systemctl --user stop gufo@$m"; sleep 5
ssh misty "systemctl --user is-active gufo-rank1@$m" >/dev/null && ssh misty "systemctl --user stop gufo-rank1@$m"
true
