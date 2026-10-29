# Reproducing the recorded Gufo/Donnerkeule TP2 setup

Recorded2026-10-28 on EVO-X2/Corsair Strix Halo128GB, two40Gb/s USB4 cables,
four native rails, custom Linux7.3.0-strix-test with the separately documented
Thunderbolt lifetime fix, user-local ROCm7.14.1. This is bounded qualification,
not a recommendation to replace other users' kernels or a claim of overnight
stability. The hardware/module tests made no system ROCm, core-module or boot
changes. Both machines were restored to our81C6 diagnostic baseline afterward.

## Gufo source now available

[Bundle and commands](../patches/gufo-tp2-baseline/README.md) provide the exact
pinned source patch, checksum-checked build helper, MIT notice and independent
head-gather probe. It removes the previous dependency on a private Gufo binary.
No fusion/FIFO/8K experimental code is included. The recorded b288 executable
remains historical provenance; the portable build has a different whole-file
hash.70/71 embedded GPU text objects match it exactly; the remaining object's
only instruction changes are eight PC-relative addresses for source-path/error
strings in a DeepSeek MMQ unsupported-device diagnostic object. This inspection
is not a substitute for model tests or complete descriptor/metadata equivalence.

Fresh build, helper invocation, shared formatting check and clean-index patch
application passed. Forty exact head-gather cases/rank and ten same-world
model-request comparisons passed. Model-parity detail and raw per-request
metrics are in the accompanying JSON/CSV summaries. Four5%/3ms timing flags
remain inconclusive; cached/cold continuation equivalence and general model
accuracy are not established. Do not market this as a new speed or quality win.

## Driver from PR3: fresh two-cable qualification

The tested driver source is fix/native-rc-ordering at
7e6c0b3d22507b8cd395faa1f701cbefbb0a757c, containing the RC ordering and abandoned
TX retirement fixes ([PR3](https://github.com/LostVector/donnerkeule/pull/3)).
It is the same experimental Thunderbolt driver with our fixes, not a separate
AMD/Ubuntu driver. The measured artifact has sourceversion52C08D90CDFE35DF0026149.
Our usual81C6 build has additional diagnostic trace sites. Do not build the
polling/docs branch alone and assume it contains PR3's prerequisite fixes.
Use that pinned driver source with the existing installation/build instructions;
keep the [core lifetime fix](kernel-xdomain-lifetime.md) separate.

Fresh checks passed:12 paired host/GPU-staged cases,48000 completed RTT samples,
eight write ordering/overlap/physical-alias cases, four intentional peer exits,
four fresh reconnects and five matching Gufo requests. Clean transport/order/
recovery/model intervals had no tracked error/retry increases. Each induced
peer-exit case separately recorded one receiving-side ACK error and reorder
timeout; these are retained and are not included in clean-traffic claims.
Captured kernel logs contain no Oops/BUG/panic/refcount warning patterns.
This does not qualify all verbs, DMA-BUF/GPU-direct, arbitrary kernels or rare
long-running faults.

At8KiB hot RTT, the first before/driver/after medians were39.32/38.25/37.75us.
After300us requested idle they were75.16/131.90/70.05us, so a second unchanged-
protocol baseline/driver/driver/baseline run was required. Its idle medians were
132.63/133.07/66.06/134.42us; hot medians38.81/37.79/36.78/38.17us. Thus no
consistent driver-specific idle slowdown was established. Both endpoints sleep
in this benchmark; completed RTT includes peer readiness/wakeup and validation,
not pure wire latency. Actual initiator outside-RTT time was~353us for requested
300us idle. Do not hide the phase-dependent distribution or add stage medians.
The repeat's32000 additional RTT samples had no tracked error/retry increments.

## Provider source provenance

The tested private runtime's provider binary equals the build artifact generated
from public provider source in the same PR3 commit. All five provider files
match. Applying that commit's three packaging patches to public rdma-core
558104fc33266abe8a9deb50a81769d1a72fbf72 reproduces all seven patched files of
the tested build tree. No unpublished provider change is needed. Use the matching
libibverbs/provider pair; do not drop a plugin into an incompatible rdma-core.
The measured build used RelWithDebInfo,ENABLE_STATIC=0,NO_PYVERBS=1,
NO_MAN_PAGES=1,BUILD_TESTING=OFF and a user-local install prefix. See the fork's
[provider patch instructions](../../packaging/rdma-core-patches/README.md).

## Limits and retained evidence

JSON/CSV summaries are in portable-tp2-baseline/. Full raw traces, controller
scripts and endpoint logs are retained in the local investigation, not bundled
here as a complete standalone latency benchmark. The source bundle and
head-gather test are self-contained relative to the pinned Gufo source and
installed dependencies. The two-node serving launcher/settings are reusable;
this is not an automatic firmware/kernel/MOK/network installer.
