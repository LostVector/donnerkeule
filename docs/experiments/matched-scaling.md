# Fresh matched TP1 / TP2 —2026-10-28

Direct ABBA measurements after BOTH hosts passed the Thunderbolt lifetime fix.
Same head-gather2 b288 binary, private ROCm7.14.1, identical fully rehashed four
Unsloth UD-Q4_K_XL GGUF shards (111334654784bytes), original disk-backed n-gram
policy, no full-table warming. Native81C6 RXoff/TX32, fixed coreBDA, two40Gb/s
cables/four rails. No profiler was active during these measurements.

| Metric, tokens/s | TP1 EVO | TP2 | Scaling |
|---|---:|---:|---:|
|2K prefill|1732.32|1809.28|1.044×|
|8K prefill|1682.71|2069.10|1.230×|
|32K prefill|1642.88|2020.31|1.230×|
|Short single-stream decode|29.56|40.09|1.356×|
|Code single-stream decode|29.62|40.37|1.363×|
|Two-request aggregate, including prefill|45.84|58.59|1.278×|

Prefill: context65536/CLI chunk2048 (backend4096 per bank), exact2K/8K/32K prompts,8 outputs. Decode profile:
context4096/chunk512,256 outputs. Settings are identical across world sizes
within each profile; the profiles themselves have different contexts/chunks.
MTP/thinking off, greedy temperature0/seed1,2 sessions,1GiB prompt-cache budget
with per-request cache reuse disabled. Three repetitions per case per server
start; two independent starts per world size. Reported values are medians of
start medians. Error bars in matched-scaling.png/svg show start range, not CIs.

## Correctness, work and limits

96 measured requests plus8 warmups completed. Every request reports uncached
prompt work and no draft tokens. Within-world repeats match all24 output/work
records per world. Cross-world24 work records match;9 prefill output hashes match,
but all15 AR continuation hashes differ. This is practical generative scaling,
not token-forced identical execution or an accuracy certification. Partitioned
accumulation changes greedy choices and consequently expert/PLE access paths;
therefore not every low-level operation is held identical by the common token
budget. Full quality qualification remains separate.

All8 rank-phase counter intervals have zero tracked error/retry increments.
Raw per-request tails remain in results/model-scaling/matched-*.jsonl and
serving.csv; median gains do not certify p99 latency or long-duration stability.
The older128-output starting baseline is historical context, not a controlled
before/after of this256-output experiment. TP also changes internal execution,
including graph eligibility; this is not a pure transport speedup.

## Public summaries

[Serving CSV](matched-scaling-serving.csv), [output/work audit](matched-scaling-quality.json),
[counter audit](matched-scaling-counter-audit.json). Full raw request records and
profiler traces remain in the local investigation workspace. Lab scripts and
pinned Gufo builds are not bundled in this driver fork; these summaries are
measurement evidence, not a fresh-system installer.
