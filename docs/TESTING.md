# Driver regression tests

Run `make test` on an ordinary Linux development machine. No root privileges,
Strix Halo, GPU, network peer or kernel headers are needed. A C compiler with
ASan/UBSan, matching GCC/gcov and Python3 are required. These are host regressions, not a simulation
of the complete kernel or a certificate that the transport is correct.

```
make test
make test TEST_OUTPUT=/tmp/driver-tests-new-run
python3 tools/ci/run-smoke.py --list
python3 tools/ci/run-smoke.py --suite rx-error-recovery --suite native-rc-order
```

Output directories must be new: earlier failures are never overwritten. JSON,
JUnit and per-group logs preserve compiler errors, assertions and failures.
Every selected group runs; any failing group fails the command. Unknown groups
are errors. CI runs this command on each PR/main push, independently of the
existing distribution module/package builds. CI artifacts retain failed logs.

## What is covered today

The runner contains24 groups, including an11-test Framework Desktop group.
Many C regressions extract actual production functions and compile them with
small host shims for kernel types, locks, allocators and hardware callbacks.
Other groups exercise protocol code directly, provider patches or CLI tools;
some source-layout assertions enforce wiring. These forms have different limits.

| Failure class | Existing executable regression | Limits |
|---|---|---|
| CRC/overrun interpreted as valid payload | `rx-error-recovery`: actual RX callback; bad flags, short frame, credit gap, canceled receive, repost failure | DMA/NHI behavior is mocked |
| Retry cancellation poisons a completed QP | Same group: actual send callback; completed cancel, ACK-won race, genuine failure, reference retirement | Controlled schedules; not exhaustive kernel concurrency |
| Late older WRITE overwrites newer WRITE | `native-rc-order`: actual SQ helpers, reverse fragments/partial overlap, PSN wrap, independent QPs, caller ownership, closing gate;1000 concurrent races | Host scheduler and memory, not USB4 hardware |
| Abandoned build never arms retry | `native-tx-retirement`: old-function negative control and actual helper, both retirement orders, RNR/error/closing,1000 final-retirement races | No real DMA callbacks |
| Lost RX progress or teardown wakeup | `framework-desktop` native RX progress tests: actual helpers, rearm/budget, stop/teardown and single-consumer wiring | No real IRQ delivery |
| Nested staging reuses an outer lease | Framework credit-cache allocator test | Allocation/coherency modeled by host mocks |
| Control lifetime, CQ exhaustion, allocation cleanup | `verbs-control-lifetime`, `verbs-post`, `verbs-rx-state`, framework provider-CQ tests | Not a complete verbs conformance suite |
| Stale peer identity/session snapshot | `descriptor-session`:11 valid/stale/malformed/partial CLI cases | Does not emulate kernel hotplug |
| Protocol sequencing/reliability/configuration | `proto`, `reliability`, `identity`, `config` | Protocol invariants only |
| Test infrastructure reports false success | `test-runner`: failed compilers, continued groups, JUnit failures, unknown suite, retained prior logs | Runner mechanics only |
| TX packet pool reuse/ownership | `fault-injection`: actual allocator/release/completion;128 release states,32 completion states,10000 concurrent allocations/releases and reentrant completion | Host mutexes and mocked DMA unmap/free, not NHI DMA reuse safety |
| Combined corruption/lifecycle/credit failures | Same group:146432 deterministic RX events/oracle steps; descriptor flags×length×cancellation×state, seeded loss/sequence wrap and restart | One replayable seed and selected boundaries, not exhaustive transport execution |

Descriptor-session, native RC ordering and abandoned-build retirement were
previously runnable standalone but absent from the common runner. They are now
part of the ordinary test command. The CRC/send-callback regressions were already
registered. Sanitizers remain enabled in their C harnesses. Framework tests that
honor TBV_TEST_SANITIZERS receive it by default; several older harnesses do not
yet honor it. This is not whole-driver sanitizer coverage or a TSan claim.

## New fault and coverage gate

```
python3 tools/ci/run-smoke.py --suite fault-injection --output /tmp/new-fault-run
```

This is new driver behavior testing, not only registering older cases. It
extracts11 actual functions: RX completion/frame validation, credit capacity,
opcode/credit validation, path sequence recovery, TX retry completion, pooled
packet allocation/release and TX frame completion. Independent event/state
oracles assert rejection before placement, no credits on failed repost, RAW
fail-closed, bounded credit recovery across wrap/restart, owned failure handling,
reference retirement, reset ownership before reuse and callback completion.

Current GCC15 result: **250/250 compiled lines and168/168 branch outcomes** in
those11 functions. Each targeted function must retain at least95% line and
branch coverage. The explicit target list is in `fault-injection-smoke.py`.
This is NOT whole-driver coverage: kernel primitives and hardware are shims,
logging is disabled, and functions outside that list are not in the denominator.
Extending the critical path requires adding its functions and tests, not assuming
this percentage covers new code elsewhere.

The identical input corpus also runs in separate ASan/UBSan builds. Coverage is
measured without sanitizers so their generated error edges do not inflate the
production branch denominator. There is no claim about actual kernel locking,
IRQ context, DMA coherency, real frees or races beyond the tested host schedules.

Ten isolated mutations remove CRC rejection, RAW poisoning, credit-restart
bounds, repost-error handling, completed-cancel protection, ACK-won QP protection,
pool callbacks, frame-owner clearing, callback-before-pool-reuse ordering, or DMA
unmapping. All ten compile and are rejected by test assertions. Compiler crashes
or sanitizer startup failures are not accepted as a detected mutation. These
mutations affect only temporary test copies, never the actual driver source or
live module. Tests also retain the earlier abandoned-frame negative control.

`TEST_OUTPUT` retains normal and mutant logs, raw gcov JSON, generated harnesses,
commands and `fault-injection/coverage.json`, alongside the common JSON/JUnit.
The seed for the RX loss stream is0x63a8d471; failing event matrices are deterministic.

## Build out fault coverage next

1. Extend the new deterministic event corpus into a general callback scheduler
   around the real RX/TX helpers. Exercise multi-QP loss, duplication, reordering, delayed ACK, retry-before-
   completion, cancellation, resource failure and close at each legal boundary.
   Replay failing seeds. Check an independent state-machine oracle: exactly-once
   completion, no placement of rejected payload, correct ordering/credits,
   bounded references and no callback after buffer release.
2. Extend the new TX-pool exhaustion/reuse tests to disconnect during every
   outstanding ownership state. Neither the new pool mocks nor staging-cache
   tests establish TX-ring DMA reuse safety.
3. Add KUnit or equivalent kernel-context tests for locking/workqueue lifetimes,
   with lockdep/KASAN and separate KCSAN runs where supported. Preserve source-
   extracted tests as the quick gate; do not claim their mocks prove kernel ABI,
   IRQ/DMA coherency or memory-order behavior.
4. Expand the existing two-host integrity checker into a bounded matrix of sizes,
   queue depths, overlap, directions and one/two cables. Verify every payload,
   sequence, local completion and peer validation before reuse. Inject faults
   through a bounded explicit test hook; distinguish those from physical CRCs.
5. Replay real Gufo prefill/decode, cache, cancellation and peer-loss histories,
   then run a separate overnight stress job. Correctness and recovery gate first;
   measure latency/performance in uninstrumented matched runs afterward.

Each production fix should add a deterministic regression that fails with the
fix removed, plus a documented hardware test when the failure crosses a DMA,
IRQ or transport boundary. Keep negative controls, failed seeds, module hashes,
kernel/firmware/link state and raw results. Hardware unavailable means not run,
never a fabricated pass. Do not make a permanent corruption/fault mode part of
normal driver operation.

## Current hardware evidence

November5's fixed844E module completed4096 verified19,630,144-byte transfers
(74.883GiB), rejecting11 real CRC-marked completions and recovering with6 WR
retransmissions. The same diagnostic baseline failed both directions after a
successful retransmission poisoned the next post. An actual dual-cable DFlash
run subsequently completed all14 requests while recovering from two more CRC
events. Those are bounded recovery observations, not overnight qualification,
a performance improvement claim or an explanation of the physical CRC origin.
