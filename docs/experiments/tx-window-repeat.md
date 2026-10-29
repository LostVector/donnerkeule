# TX frame window32 versus128: small gain reproduced

**Result:**128 is a useful optional throughput setting, not a failed candidate
merely because the gain is below5%. Balanced32/128/128/32 repeats reproduce a
small bulk improvement and~1% median inference gain. Tail behavior remains mixed;
this is not a universal latency or production qualification.

## Fixed configuration

One USB4 cable,40Gb/s trained in both directions; native baseline81C6,
RX pollingoff, registered host windows and GPU-staged exchange harness. Same
frozen b288 head-gather2 Gufo and UD-Q4_K_XL weights, AR (noMTP), original disk-
backed n-gram policy. Context/chunk4096/512 for decode,65536/2048 for prefill,
two resident sessions, cache bypass, greedy, thinkingoff. Only the native
**DMA-frame** in-flight cap changes; verbs depth and buffer registration remain
fixed. This is not a verbs WR-depth sweep.

## Before/after: medians of the two phase medians

| Metric |32|128|Change|
|---|---:|---:|---:|
|40MiB bulk Corsair→EVO, Gb/s|19.63|19.92|+1.48%|
|40MiB bulk EVO→Corsair, Gb/s|20.34|20.76|+2.06%|
|2K prefill, tok/s|1782.31|1796.26|+0.78%|
|8K prefill, tok/s|1736.14|1757.03|+1.20%|
|32K prefill, tok/s|1711.33|1730.69|+1.13%|
|Short C1 AR decode, tok/s|40.011|40.444|+1.08%|
|Long C1 AR decode, tok/s|39.983|40.320|+0.84%|
|Code C1 AR decode, tok/s|40.181|40.605|+1.05%|
|C2 cohort output, tok/s|60.424|60.917|+0.82%|

Both128 bulk runs beat both32 runs in each direction. Individual bulk pairs
(a1,b1,b2,a2) are19.71/19.88/19.96/19.55 and20.38/20.72/20.80/20.30Gb/s.
Only two independent starts/setting: promising repeatability, not a precise
confidence interval or sustained-load/thermal qualification.

## CPU and tails

Whole model-phase busy logical cores (EVO/Corsair), **including model loading,
warmup and teardown**, are3.358/3.374→3.391/3.432 for prefill and2.430/2.446→
2.484/2.459 for decode. Bulk CPU changes are small: Corsair sends1.855→1.900;
EVO receives1.568→1.619; EVO sends2.087→2.056; Corsair receives1.352→1.361.
These are whole-host /proc/stat samples, not driver-only cost or watts, and are
not comparable to prior prefill-only CPU windows.

GPU-staged completed RTT, median across phase percentiles (us):

|Size/idle|32 p50/p99|128 p50/p99|
|---|---:|---:|
|8KiB hot|109.44/226.18|109.13/193.50|
|64KiB hot|303.86/505.17|304.60/480.96|
|8KiB after300us idle|138.55/275.68|116.62/314.12|
|64KiB after300us idle|321.69/589.30|323.56/604.36|

Hot tails improve in these samples; idle p99 worsens. Do not call128 a universal
latency improvement. Full8/16/32/64KiB p50/p95/p99/max are in latency.csv; these
verified serial round trips include peer/GPU work, not pure one-way wire time.

15 strict metric flags across13 candidate requests remain against paired control
medians (>5% AND>3ms), including a b2 short-C1 decode8065.9ms versus7173.3ms
control reference and first2K/8K output-tail delays. Controls also have large
outliers: one C2 maximum token gap is~1.5s. No outliers were deleted. The small
median win does not explain or clear these stalls; storage/runtime attribution
would need separate tracing. No full-table warming is proposed.

## Correctness and state

All104 model requests complete;78 repeated output/work records match the first
26-record reference exactly. All24 bidirectional disjoint/overlap/physical-alias
ordering cases pass (depth128,5MiB,4rounds); overlapped/aliased cases validate
final payloads.32,000 retained GPU-staged endpoint payload checks pass across
size/idle/window starts, plus warmups. Bulk perftest measures goodput, not payload
integrity by itself. Before/after snapshot deltas show no tracked data-path
error/retry/RNR/timeout/lost/failed increases in all8 rank-phase intervals.

Both hosts restored native_tx_max_inflight32 and baseline81C6; benchmark servers
and temporary tokens removed. The permanent driver-lab runner is installed and
used successfully on both hosts; no expiring workers or further per-round sudo
rules are needed. No GRUB/kernel/ROCm changes occurred. EVO remains IOMMUoff,
Corsair translatedDMA; that condition is unchanged across this matrix.

## Recommendation

Keep128 available as an optional throughput profile: a small measured benefit
with no observed payload/order regressions and little whole-phase CPU change.
Leave32 as the restored runtime baseline until explicitly choosing a new profile.
Do not reintroduce an arbitrary5% cutoff. Before default promotion, repeat the
idle-tail and affected-request cases, ideally paired with tracing, and combine
with adaptive RX polling only in a separate controlled experiment. No source
patch is needed: native_tx_max_inflight is already writable at runtime.

## Evidence and reproduction limits

Raw artifacts and reproducible controllers are retained on the test hosts under
`~/strix-dispatch-investigation/link40/`. Published [bulk CSV](tx-window-repeat-bulk.csv)
and [serving CSV](tx-window-repeat-serving.csv) retain each phase separately.
The frozen private engine/module prerequisites remain those in TP2_TUNING.md;
these measurements do not qualify this exact combined module, polling
combinations or another model.

## Native default in the experimental fork

Set only the native Linux parameter's initial value to128. The shared fallback
limit stays32, so Apple/non-native paths retain their prior limit. Users can
select native_tx_max_inflight=32 at load time or while idle to reproduce the
control. Runtime ring-size clamping and0(disable throttle) semantics remain.

Hardware evidence uses pinned private81C6 with ordering/retirement fixes. The
combined fork includes the RC ordering and retirement fixes, but the exact
combined polling/default128 module still needs hardware qualification. Merging
into the experimental fork does not resolve the documented timing flags or
change the loaded32-window baseline on the test hosts.
