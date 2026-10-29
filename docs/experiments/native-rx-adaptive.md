# Adaptive polling: lower CPU cost, no universal decode improvement

The opt-in RX consumer now caps batches below8frames at20us. At8frames it
may use the configured cap (100us in these tests). Caps remain relative to
worker entry, with no per-arrival timer reset; configured maximum1000us,
256-frame budget, disable checks, single consumer and normal rearm unchanged.
Default remains off.20us and8frames are experimental choices, not a proven
optimum. No buffer, transport, result or dependency changes.

## Directly measured hardware results

Two separate matched ABBA rounds, four starts each. Same Flash-Next TP2 Gufo
head-gather2 binary, disk-backed n-gram policy, Linux7.3.0-strix-test,
ROCm7.14.1 and one20Gbit/s-direction USB4 cable as earlier tests. No wait
interposer during timed model runs; lightweight250ms CPU sampling only.

| Comparison | Prefill tok/s | Short C1 tok/s | C2 aggregate tok/s | EVO short-request busy cores |
|---|---:|---:|---:|---:|
| Off |1553.30|40.541|57.875|2.870|
| Adaptive, same round |1678.85|40.504|57.691|2.974|
| Full100us, separate round |1671.38|40.796|58.461|3.766|
| Adaptive, same separate round |1632.74|40.512|58.469|2.979|

Adaptive gives+8.08%prefill versus off with nearly unchanged decode. Compared
directly with full100us polling, it uses~20.9%less measured EVO busy CPU but
has~2.31%lower prefill speed. These are separate paired experiments: do not
mix controls across rounds. Busy cores are whole-system request-window CPU,
not isolated driver CPU or power. No wattage claim. Per-start raw summaries
are retained in the adjacent adaptive-off/full CSV files.

Both rounds together pass104/104 completion/token-work comparisons,
312extra work comparisons and256000measured host/staged endpoint payload
checks. No per-exchange allocation/registration; no tracked retry/error
increases in either model or exchange windows. All p50/p95/p99 and idle
results are retained locally; do not generalize old median GPU-ready gains
into a universal tail improvement.

**Still unqualified:**9/104strict phase flags against off and10/104 against
full polling (>5% AND>3ms). Some first long/C2 requests regress. Do not discard
or average these failures away. The earlier424ms interval remains unresolved.

## Separate diagnostic, not part of throughput measurements

A seven-request prefix on off and adaptive passes14/14 output/work checks,
with zero observer overflow and no tracked retry/error increases. All17client
useful-event gaps above100ms overlap~75–93ms preads and matching submission
condition waits. Inlined symbols identify NgramTable::ReadOne/Worker and
WaitRead→Executor::WaitPle. Max server gaps are114.06ms off/118.54ms adaptive.
This attributes these observed smaller stalls to n-gram-read waiting; it does
not retrospectively prove the cause of the214/424ms events in other runs.
Concurrent read durations must not be summed or treated as SSD service time.
Original disk-backed policy remains; no full-table warming.

## Scope, verification and raw artifacts

Hardware module sourceversionBB616A07CF93BF833C36C26 includes the separate
private ordering fixes and diagnostic probes. This PR is the clean functional
port, not the exact hardware-tested binary. It builds on7.3headers; actual
worker ASan/UBSan tests cover short/burst deadlines, empty entry, budget,
ordered draining, rearm and100000IRQ/disable attempts.257257window choices
also pass boundary checks in the isolated investigation. No production merge
or enablement is requested. Both machines restored baseline81C6 afterward.

Complete raw phase/latency/CPU/counter data, model JSONL, observer CSV and
symbol stacks remain under ~/strix-dispatch-investigation/driver-latency/
adaptive-poll/results and adaptive-vs-full/results. Scripts/commands are in
their READMEs. Clocks remain per host; no cross-host clock subtraction.
Before promotion: resolve individual timing gates, reproduce relevant tail
improvements, and broaden disconnect/deep-queue/sustained reliability testing.
