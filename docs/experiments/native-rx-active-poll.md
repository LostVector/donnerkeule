# Experimental bounded native RX active polling

This draft stacks on `fix/native-rx-progress` (upstream PR neuhaus/donnerkeule#2).
It adds `native_rx_active_poll_us=100` as an opt-in module parameter, effective
only on native paths using `native_rx_progress=1`. Default is zero; values above
1000 are clamped. Inspect the effective value in the RX debugfs summary.

The existing single consumer polls after at least one frame until an
entry-relative deadline. Arrivals never extend the deadline. Empty entries do
not spin. The 256-frame budget, disable checks, interrupt rearm and periodic
liveness recheck remain. No ring API is called under the queue/disable lock.
This trades worker CPU for fewer rearm cycles; it is not free throughput.

## Preliminary hardware evidence — not production qualification

Two Ryzen AI Max+395 hosts, Linux7.3.0-strix-test, ROCm7.14.1,
one USB4 link at20Gbit/s per direction, registered host buffers.
The private instrumented module sourceversion was7E27074AEAB86786BC27732;
ABBA windows were0/100/100/0 microseconds. This clean PR port has build and
host tests, but has not itself been loaded on the two hosts. The measured
private stack also contains separate ordering/retirement fixes and trace probes.
Do not attribute its whole-stack performance to this patch alone.

Gufo Flash-Next TP2, fixed head-gather2 binary SHA256
b288378fe41039f940aed5747c21c0964d9937e0f586e2cef063a2fd087f7599,
context4096/sessions2, original disk-backed n-gram policy. No table warming.
Raw per-start headline measurements are in the adjacent CSV. Median per-start
prefill improves7.11%, short decode1.11%, C2 aggregate0.91%.
All52 completion hashes/token-work fields and156 additional work fields match.
**Two of104 strict phase checks fail** on one candidate request: decode
3261.692→3639.513ms and request4579.171→4862.432ms. Its maximum token gap
is424.568ms (paired control50.012ms). Cause remains unresolved; retain this
failure and do not promote the candidate. Model-window retry counters were
not captured, so a transport timeout is only a hypothesis.

64000 separate host endpoint payload checks pass. Hot8KiB RTT p50 is
40.259/37.419us control versus33.434/33.359us candidate. Idle tails are
inconsistent. Whole-system busy CPU in this host microbenchmark increases
roughly1.5–2 logical CPUs per host; this is not isolated inference CPU cost.
Separate traces show28–31% fewer rearm calls; rearm p99 drops on both hosts,
but total rearm wall time rises on one host. No blanket stage-cost claim.
Both hosts were restored to their prior module after testing.

The complete retained investigation is under
`~/strix-dispatch-investigation/driver-latency/active-poll/results/` on the
test hosts: engine-request-phases.csv, engine-flags.json, host-audit.json,
four trace audits, and original model JSONL under results/model-scaling/.
These local artifacts are not bundled here; the CSV is a headline extract,
not independently sufficient evidence for qualification.

## Reproduction and remaining gate

Build without changing the installed module:

```
make -j8
TBV_TEST_SANITIZERS=1 python3 -m unittest discover \
  -s tools/ci/framework-desktop-tests -p test_native_rx_progress.py
```

On dedicated hosts, use existing safe unload/load procedures with
`native_rx_progress=1 native_rx_recheck_ms=1 native_rx_active_poll_us=0`
versus100. The parameter is read-only after load. Do not unload with active
QPs or force unload. Preserve an Ethernet management path and rollback module.
Repeat the affected seven-request model prefix, collecting both hosts' driver
counters around every model history and token/transport wait traces. Resolve
the424ms stall, quantify inference CPU cost, then repeat deep-queue, alias,
disconnect and sustained-load correctness before proposing upstream promotion.
