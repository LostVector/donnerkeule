# RX hardware-error rejection and successful retry retirement

## Problem and changes

A damaged Thunderbolt receive descriptor can have a valid-looking native header.
The previous RX callback did not reject `RING_DESC_CRC_ERROR` or
`RING_DESC_BUFFER_OVERRUN` before placing payload bytes. The receiver could then
publish a successful completion for data that failed the application's full
payload check. Linux's stock Thunderbolt network receiver checks these flags.

The RX callback now rejects those descriptors before parsing, copying, or ACKing
their contents, counts CRC/overrun events, and still reposts the receive buffer.
It does not derive credits from an untrusted header. A later valid framed-data
sequence accounts for a skipped sequence within the existing credit window.

An already-active headerless RAW stream cannot infer the original byte offset
from a damaged descriptor. It fails closed until the path is recreated, with
separate poison/drop counters. This prevents shifting later chunks and falsely
completing that damaged stream; it is not automatic RAW recovery. Loss of a RAW
header and broader RAW/Apple recovery remain outside the live qualification.

A second bug affects retry cleanup. A successful ACK completes a send, then
cancels surplus retry packets. Their cancellation callbacks previously marked
an ordered QP as failed even when the send was already successful. The callback
now retires its DMA accounting and reference without poisoning an already
completed QP. If an ACK wins the subsequent lookup race, an ACK-ready or retired
send is also not treated as a new fault. Genuine owned pending-send failures
still fail the QP. The ordinary zero-status TX completion fast path is unchanged.

No new tuning switch, polling policy, wire format, model code, kernel configuration
or ROCm installation changes. New debugfs counters are `data_rx_crc_error`,
`data_rx_overrun`, `data_rx_raw_poisoned`, and `data_rx_raw_poison_drop`.

## Direct measurements on two Strix Halo hosts

Hardware: EVO-X2 and Corsair AI Workstation 300, Ryzen AI Max+395/128GB each,
Linux7.3.0-strix-test with previously tested Thunderbolt core BDA6ED3. Two physical
20Gb/s-per-direction cables, four native rails, TX32, native RX progress enabled,
1ms recheck. Management Ethernet carries bootstrap and test reuse fences.

The fixed host checker uses one registered source/destination window and
41,943,104-byte WRITE_WITH_IMM messages (40MiB+64, the failed prefill exchange size),
depth1. It verifies every 64-bit payload word, sequence, completion order and safe
buffer reuse. It does not measure pure transport latency because it includes CPU
pattern generation/validation and Ethernet fences. No GPU/model is required.

| Build | Bounded result |
|---|---|
| Private reference81C6 | Same malformed RX lengths as model failure; subsequent post/CQ failure |
| DiagnosticB472, no rejection | First1,024 messages verify; reverse direction returns bad payload at sequence951 after a descriptor reports CRC+overrun |
| CRC-only872A | Two damaged frames rejected and retry ACK received, but subsequent post fails; a later2×1,024 clean run does not prove error recovery |
| Combined prototype947B | 4,096 messages each direction,8,192 total (~320GiB), all payloads verify. First direction sees5CRC drops and3retransmissions; reverse has no checked error increases. Continues correctly after actual damaged descriptors |

The hardware flags establish an erroneous RX descriptor, not its physical cause.
Cable quality, controller behavior, host pressure or another DMA path still need
isolation. The software must reject the errors regardless of that cause. These
results are correctness/recovery evidence, not token-throughput improvements,
statistical speed comparisons, overnight reliability or all-verbs certification.

The prototype's two affected callbacks are byte-identical to the versions ported
to this fork before the fix. The independently built fork module844E9145B923CA473A7AD39 also passes4,096
messages each direction (another~320GiB), including2CRC drops and1retransmission
in the reverse direction. Every word verifies and both QPs remain usable.
Unsigned artifact SHA256:
`3237eefc6dea0ba3a9eea387022de1f2d84bf0fe6db12457ab5c0a9885cb7e81`.
This pins a tested build; another compiler/kernel need not produce that hash.

A subsequent actual Gufo TP2 history passes all6prompt/cache/completion identities
with the same frozen engine/model/settings: tinywarmup,2K first/repeat, two concurrent
short requests and32Kprefill. Quant UD-Q4_K_XL, shared-Q8_0 MTP enabled,
context65,536, two sessions,1GiBcache,CLI2048/backend4K, temperature0/thinkingoff.
The6-request model interval has no checked driver error/retry increases and final
references0. It does not directly exercise CRC recovery during model execution;
that path is exercised by the preceding full-payload host test.

One-history observations are32Kprefill2,032.4tok/s, C1decode57.06tok/s and C2
whole-request aggregate71.01tok/s. These are not speedup claims. Compared with
an earlier delivered identical-engine history, one C2request retains+248.0mswall
and+225.7msdecode flags (also a duplicate raw metric); both C2requests have
changed adaptive MTP draft work. No randomized matched parent-driver timing
control has been run. Keep all3strict>5%AND>3ms flags and6adaptive-work differences
visible. Normal-path performance, all-verbs/RAW and overnight qualification remain
open. Both hosts were restored to the prior81C6/TX32 idle baseline after testing.

## Reproducible software checks

```
python3 tools/ci/rx-error-recovery-smoke.py
python3 tools/ci/run-smoke.py
make -C kernel -j6
```

The new CPU tests compile the actual production RX, sequence-credit, native frame
validation and send-completion callbacks with hardware shims under ASan/UBSan.
They cover CRC/overrun before payload processing, buffer repost, exact sequence-gap
credit recovery, canceled descriptors, malformed lengths, repost failure, active
RAW fail-closed behavior, completed retry cancellation, ACK-retirement/ACK-ready
races, normal TX completions and genuine pending errors. The previous send
callback fails the completed-cancellation regression case; the corrected one
passes. These tests do not emulate USB4 hardware or certify RAW recovery.

Keep management access and the qualified kernel/core available. Stop all QPs
before normal driver unload; never force unload. Use a separate root-owned signed
candidate copy if Secure Boot requires it, preserve the baseline image and verify
module identity, link/rail state and final references on both hosts. No system
installation or automatic driver replacement is part of this patch.

## Host reproduction tool

The test source is included as `tools/tbv-host-integrity.cpp`. Build against your
qualified libibverbs/provider headers and library, then run in the matching native
provider environment on both hosts:

```
c++ -std=c++20 -O2 -pthread tools/tbv-host-integrity.cpp -libverbs -o tbv-host-integrity
./tbv-host-integrity --self-test
# Receiver; replace the placeholder with its management IPv4 address:
timeout 180 ./tbv-host-integrity 0 RECEIVER_IPV4 28781 1 41943104 4096 disjoint
# Sender, from the other host, using the same receiver address:
timeout 180 ./tbv-host-integrity 1 RECEIVER_IPV4 28781 1 41943104 4096 disjoint
```

Set `STRIX_RDMA_DEVICE` when the device is not `usb4_rdma0`. This native test uses
GIDindex1. With a private rdma-core runtime, add its include/library paths at build
and select its provider/runtime library paths at execution. It is not a GPU or
RDMA latency benchmark. Reverse sender/receiver roles for the other direction.
Use finite process timeouts, capture before/after driver counters and both logs,
and reject nonzero exits or unverified payloads. Expected recovered CRC/drop/retry
counts are kept separately from unexpected errors; a payload pass with retries
must not be labeled a zero-error performance sample.

## Additional matched Linux qualification (2026-10-31)

The same 844E module was tested on the existing Linux 7.3/BDA core dual-Strix pair,
with matched TTM/IOMMU-off flags, TX32, RX progress 1/recheck 1 ms and active polling 0.
A Corsair-only cable crossover changed the physical layout; one link negotiated
40 Gb/s per direction and the other 20 Gb/s. This is not rate-matched to the earlier
hardware-error runs. Four rails remained active per host.

A fixed parent/fix/fix/parent sequence used the identical ccdb9495 Gufo engine,
UD-Q4_K_XL plus shared Q8 MTP, context 65536, sessions 2, chunk 2048 and 1 GiB cache.
All 24 canonical prompt/cache/output identities matched; all 8 C2 requests executed
at width 2. All 8 transport counter windows were error-free. No per-request phase
regression exceeded both 5% and 3 ms against either parent start. The prior unmatched
+248 ms C2 flag did not recur; this does not universally clear it.

| Metric | Parent starts | Fixed starts |
|---|---|---|
| 32K prefill tok/s | 2046.62 / 2035.45 | 2039.47 / 2037.48 |
| C1 MTP decode tok/s | 56.995 / 56.958 | 57.350 / 56.898 |
| C2 whole-wave tok/s | 69.800 / 68.568 | 71.254 / 70.660 |

C2 draft work differs, with 24 retained metric comparisons against the two parent
starts (reused observations, not independent samples). These four starts do not
establish a speedup, statistical equivalence or default-adoption qualification.
Built parent 667ac2 and candidate base 3dbaaa kernel sources are byte-identical.

A separate host-only run loaded 844E once per host for 26 cases:

- Eight WRITE_WITH_IMM cases: each direction, depth 128, 5 MiB/WR, two rounds,
  disjoint/full overlap/partial overlap/physical-alias destinations.
- Six SEND_WITH_IMM cases: each direction at depth 1/8 KiB/64 rounds,
  depth 32/64 KiB/32 rounds and depth 128/5 MiB/two rounds. Posted receive SGEs,
  byte lengths, immediate sequence, every payload word, CQ order and reuse checked.
- Four deliberate sender exits, each direction after 8 or 128 accepted WRs in a
  depth 128/5 MiB batch. Sender exits 99; receiver exits 2 with WC status 5 (flush).
  Each receiver records one ACK error, one reorder drop and one reorder timeout.
- Eight subsequent SEND/WRITE checks on fresh QPs: depth 8/64 KiB/32 rounds.
  All pass without a driver reload. The old failed QP is not recovered.

All 22 successful cases have zero checked transport-error/retransmission deltas.
Overlapping/aliased WRITE tests check the final observable destination state,
not overwritten intermediate payloads. Expected exit-window errors are retained
separately. Both approved 81C6/TX32 drivers are restored idle, with four rails;
checked kernel crash/lockup patterns are absent. No Halogen restart.

These Linux TBV-framed tests do not exercise headerless RAW, physical cable loss
or injected CRC descriptors. The earlier actual damaged-frame runs remain the
hardware-error recovery evidence. Production-callback CPU tests were rerun and
pass active-RAW fail-closed checks under hardware shims; lost RAW headers and
automatic RAW recovery remain unqualified. This is not all-verbs/overnight
certification. No production source or default changed in this follow-up.

The local investigation retains frozen protocols/source hashes, exact commands,
per-request data and all 26 host logs/counter windows in
`tp-cache-retention/driver-parent-timing-v1/` and
`driver-frame-investigation/recovery-qualification-v1/`. Those lab harnesses are
not bundled in this PR; the shipped WRITE integrity tool above remains the
portable reproduction entry point. The checked-in qualification record below
preserves the host-case outcomes and source/binary hashes for review.

Review record: [rx-error-retry-qualification.json](rx-error-retry-qualification.json).
