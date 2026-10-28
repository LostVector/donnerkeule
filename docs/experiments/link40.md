# 40 Gb/s dual-Strix qualification and serving results

Measured 2026-10-27. **Outcome C: mixed.** Local HIP launch cost does not explain a fixed millisecond TP penalty. Native receive progress and scheduling matter, but the remaining costs include real communication, replicated GPU passes and model execution. All finite experiments below completed; negative results remain retained.

## Stack and scope

EVO-X2 + Corsair AI Workstation300, Ryzen AI Max+395, 128GB unified memory and32 logical CPUs each; kernel7.3.0-strix-test, isolated ROCm/HIP7.14.1. One cable, controller path1-2, both directions20Gb/s/lane ×2 lanes =40Gb/s. Physical front/rear labels cannot be inferred from the controller path.

Frozen Gufo head-gather2 SHA256 `b288378fe41039f940aed5747c21c0964d9937e0f586e2cef063a2fd087f7599`. Qwen3.8-Flash-Next Unsloth UD-Q4_K_XL; shared-Q8_0 MTP sidecar SHA256 `5ff54097406a905cf3a724c709124ceb0e3e10235ee862298969e91c96fa96e6`. Original disk-backed n-gram behavior, no full-table warming. No GLM/DeepSeek performance claim.

Short serving: ctx4096/chunk512, two slots; prefill/deep: ctx65536/chunk2048, two slots. C4 has four total slots, DP2 two independent single-node replicas with two slots each. Temperature0/seed1, thinkingoff, cache bypass,256 output tokens for retained decode tests. The CLI chunk is not necessarily the executed kernel row count.

## Direct measurements

| Test | One EVO | TP2 | Interpretation |
|---|---:|---:|---|
| Short AR C1 prose, tok/s |29.57|40.24|~1.36×; matched repeated starts|
| Short AR C2 cohort output, tok/s |45.81|60.43|~1.32×; includes TTFT/prefill|
| Short MTP C1 code, tok/s |67.72|72.24|Different speculative policy/cost balance|
| 32K MTP code, tok/s |58.14|76.73|Two retained requests per configuration, one start|
| C4 mixed AR aggregate, tok/s |64.43|84.55|Three cohorts, four requests each|
| C4 mixed MTP aggregate, tok/s |84.47|107.15|Three cohorts, four requests each|

DP2 mixed aggregate: **88.06 AR /127.96 MTP tok/s**. This is aggregate output across four requests and two independent replicas, not single-stream speed or TP2. The retained prompts match C4; warmup prompts differ, so the +19.4% versus TP2 MTP is exploratory, not a perfect controlled A/B. DP2 only applies when the model fits one node.

32K prefill, six retained requests per mode across two starts:

| Mode | tok/s | EVO busy cores | Corsair busy cores |
|---|---:|---:|---:|
| One EVO |1644.05|1.26|idle|
| TP2 polling off |1708.23|4.48|4.46|
| TP2 adaptive20/100us |1846.16|4.91|4.75|
| TP2 full100us |1865.54|4.93|4.84|

Adaptive gains8.1% versus off; full gains9.2%, only1.05% beyond adaptive. CPU is whole-host busy logical-core equivalents from /proc/stat, not driver-only CPU or watts; per-host monotonic calibration and trimmed intervals avoid clock mixing. Small differences between adaptive/full are near start variation. Two full-mode8K decode-tail strict flags remain; no prefill strict flags. This is not unconditional production qualification of full polling.

## Transport ceiling and rejected TX-window change

40MiB one-way bulk: ~19–20.7Gb/s. Additional QPs do not unlock40Gb/s one-way. Full-duplex reaches41.1Gb/s **aggregate**, consistent with ~20Gb/s in each direction. Both logical rails carry traffic; physical link training alone does not establish payload bandwidth.

Host completed RTT at8KiB: hot p50/p9938.97/47.67us; after300us idle95.41/177.60us. GPU-staged hot8KiB150.86/263.48us. These are serial verified round trips including peer work/fences, not one-way wire latency. All8/16/32/64KiB p50/p95/p99 and stage samples are in transport/latency.csv and raw exchange CSVs. DMA-BUF unsupported; no GPU zero-copy claim.

TX frame window32/64/128: mean one-way19.88/19.905/20.24Gb/s. Best gain **1.81%**, below the predeclared5% model-test gate. Depth128 disjoint/overlap/aliased-MR checks pass for every window/direction. The model A/B was correctly skipped; keep32. No driver code change is justified by this test.

## Correctness, tails and limits

AR/MTP audit:114/114 same-world paired outputs and work records match, including warmups. This is bounded fixture evidence, not full accuracy/logit equivalence, sampled-mode qualification or cross-world numerical parity. Repeated AR starts have their own within-world audit. Transport includes64,000 measured payload checks plus ordering tests; bulk perftest throughput is not itself a payload-integrity oracle.

Counter audit:62 before/after snapshot pairs, zero increases across the common data-path error/retry/RNR/timeout/lost/failed fields. This covers the captured intervals, not an uptime-wide guarantee.

Retain cold deep-prefill tails: TP2 AR prose31.29 then38.79tok/s, MTP prose46.74 then54.14. A105ms AR gap and119.8ms MTP maximum remain. Disk I/O is a hypothesis here; historical n-gram stalls do not prove the cause of these particular events. No outlier deletion or full-table warming.

Public full-poll flag has build/sanitizer validation. Hardware tests used pinned private7E27(off/full) andBB61(adaptive) modules with additional ordering/retirement work; do not claim the exact clean public module was hardware-qualified. Baseline81C6 is restored. Historical20Gb/s comparisons changed port/controller/date and cannot establish a pure link-speed gain.

## Attribution and next implementation target

Historical CPU ledger: verbs post medians9.42/11.68us; ~14ms published-to-worker delay is backlog behind earlier work, not isolated wakeup latency. Worker-to-publication~20.6ms for large prefill exchanges includes preceding GPU work and peer readiness/transfer. A40MiB transfer alone needs16.78ms at20Gb/s. These overlapping stage totals cannot be summed as avoidable overhead.

Gufo remains the promising patch target: safe TP-compatible HC/inject/combine fusion can remove extra memory passes; inspect ownership, remote completion and arithmetic before implementing. Flat peer-add improved its component9–19% but end-to-end32K1814.37→1810.74(-0.2%): rejected. Naive wait/add fusion and extra-collective splitting also failed earlier gates. Full-vocabulary greedy MTP head is already TP-sharded; do not propose that as new work.

Next bounded experiment: reproduce one HC fusion mechanism on the real TP path, component correctness first, then matched uncached32K prefill and256-token decode. Separately investigate why the native transport caps near20Gb/s one-way using DMA-ring occupancy/credit accounting, not another blind TX-window increase. No new graph microbenchmark or unsupported hardware-counter workaround is justified. DRAM bandwidth remains unmeasured after the profiler/kernel ABI hang; disabled workaround must stay disabled.

## External comparison (context, not A/B)

[Dual-Strix reference](https://github.com/doctomoe2019/strix-dual-exp) uses the same quant choices (UD-Q4_K_XL/sharedQ8 MTP), custom thunderbolt_stream and different Gufo patches/harnesses. It reports~2330tok/s32K prefill versus our1866; this suggests an optimization gap, not a quantization explanation. Its matched AR scaling is~35%, close to our short AR36%; its near2× headline mixes harness/corpus conditions. File-hash identity, power, prompts and executed geometry are not matched. No transport migration or system-wide installation was attempted.

## Evidence and reproduction limits

Exact raw results and controllers are retained on the test hosts at
`~/strix-dispatch-investigation/link40/`; raw serving logs are in
`~/strix-dispatch-investigation/results/model-scaling/link40-*`.
Published summaries: [serving CSV](link40-serving.csv), [prefill/CPU CSV](link40-prefill-cpu.csv).
The pinned private Gufo/driver stack is not a fresh-checkout reproduction promise.
Use [the launch guide](../TP2_TUNING.md) for supported starting profiles.
