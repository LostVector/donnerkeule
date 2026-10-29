# Two active USB4 cables: prefill scales, decode stays flat

Measured2026-10-28 on EVO-X2 + Corsair AI Workstation300. Same frozen Gufo,
UD-Q4_K_XL/shared-Q8 MTP, native81C6, TX32, pollingoff and original disk-backed
n-gram policy. AlwaysTP2/world2. Corsair normal translatedNHI, EVOIOMMUoff.

Both physical cables remain connected/trained40Gb/s in each direction. Native
domain mask2 selects the originaldomain1 cable;3 selects both0/1. The controlled
sequence is one1/two1/two2/one2, two independent server starts per setting. Thus
this is one versus two **active** cables, not a physical unplug/retrain comparison.
Both masks use the same signed module and mathematical/model execution paths.

## Direct measurements

Medians of two start medians; full per-start data are in results/serving.csv,
bandwidth.csv and latency.csv.

|Metric|One active cable|Two active cables|Change|
|---|---:|---:|---:|
|Corsair→EVO40MiB one-way bulk,Gb/s|19.80|42.74|+115.9%|
|EVO→Corsair40MiB one-way bulk,Gb/s|20.38|43.24|+112.2%|
|Uncached32K prefill,tok/s|1707.74|2022.55|+18.43%|
|AR short C1 decode,tok/s|39.636|39.660|+0.06%|
|AR C2 cohort output,tok/s|60.329|60.020|−0.51%|
|MTP prose C1 decode,tok/s|53.563|51.314|−4.20%|
|MTP code C1 decode,tok/s|72.235|72.048|−0.26%|

Both two-cable32K starts are~2022–2023tok/s; controls~1707–1709. Bandwidth
more than doubles while ordinary decode is flat. MTP prose slows in these runs;
this is not a general decode speedup. Keep all workloads rather than choosing
only a favorable token rate. Context/chunk65536/2048 for prefill,4096/512 and
two sessions for decode;256outputs,3repeats per case. No cache reuse/full-table
warming. Not evidence for256K context or another model/engine.

## Latency meaning and CPU cost

Hot GPU-staged completed RTT:

|Payload|One,p50|Two,p50|
|---|---:|---:|
|8KiB|108.87us|108.96us|
|16KiB|186.43us|177.45us|
|64KiB|304.12us|277.38us|
|512KiB|758.13us|514.01us|

This is verified GPU-staged round trip, **not pure wire latency or a one-way
collective**. Timer begins after initiating prepare/producer completion. It
includes staging, sends/CQ waits, peer GPU validation before reply, return path,
and local GPU validation. Peer-side preparation can overlap the initiator's wait.
The~66us wait field includes peer work. Independently computed stage medians
cannot be summed/subtracted to recover wire time.

Typical decode exchanges~10KiB fall below the current64KiB block-striping
threshold and stay on one rail. A10KiB payload's serialization at20Gb/s is~4us;
extra bandwidth cannot remove host/GPU dependencies or accelerate model kernels.
The head-shaped512KiB path does improve, but that is only part of the token step.
Actual Gufo small-collective profiling remains needed before precise attribution.

Whole model-phase busy cores,EVO/Corsair: prefill3.343/3.342→4.173/4.984;
AR2.485/2.448→2.472/2.521; MTP2.301/2.259→2.272/2.335. These32-logical-CPU
hosts are measured through /proc/stat including load/warmup/teardown, not pure
prefill CPU, driver-only cost or watts. Faster fixed work also changes averaging
duration. Busy CPU seconds and elapsed sampling seconds are separate CSV fields.
Hot8KiB p99 rises184.83→199.30us; idle p99 falls479.07→371.04us. All sizes,
p95/max and tails remain.34 strict model metric flags across26 candidate requests
remain against paired controls (>5% AND>3ms); do not claim universal latency wins.

## Correctness, recovery and state

168 model requests complete;126 cross-start/cable output/work records match the
42-record first control. No output-work mismatches.24 ordering/overlap/alias
cases and40000 retained GPU-staged endpoint payload checks pass, plus warmups.
All8 rank-phase counter intervals show no tracked error/retry/RNR/timeout/lost/
failed increases. Quick preliminary both-cable checks are separate raw artifacts.
Perftest is throughput evidence; it is not a payload-integrity oracle by itself.

Both hosts restored approved81C6 with ordinary all-domains selection and TX32
after the matrix, leaving both physical cables connected. The next combined-
profile experiment is separate and may be running; do not interpret a later live
module as this matrix's final state. No persistent boot/profile configuration was
installed. Exact clean public PR modules and active-transfer disconnect injection
are not qualified by this bounded comparison. Kernel/ROCm/boot flags unchanged.

## Published evidence

[Per-start serving CSV](two-cable-serving.csv), [bulk/CPU CSV](two-cable-bulk.csv),
and [latency quantiles](two-cable-latency.csv). Raw data, controllers and full
artifact hashes remain on the test hosts under~/strix-dispatch-investigation/
cable-scaling. See [the one-shot guide](../TP2_TUNING.md) for configuration and
reboot-persistence requirements. Private prerequisite and tail limits apply.
