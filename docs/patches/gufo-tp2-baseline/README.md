# Gufo source bundle for the recorded Strix Halo TP2 setup

This packages the host-staging and one/two-row half-vocabulary head-gather
changes used by our measured Gufo baseline. Optional bounded telemetry defaults
to OFF. It excludes the experimental peer/HC fusion,8K banks, FIFO scheduling,
and full-table PLE warming. Gufo's MIT notice is retained in LICENSE.

The setup is experimental. This bundle reproduces source and settings; it does
not certify model accuracy, cached/cold continuation equivalence, every sampling
mode, or identical performance on another toolchain. Our TP1/TP2 arithmetic can
produce different continuations. MTP was OFF for these qualification requests.
Earlier cached-versus-uncached continuation checks also differed in builds with
these optimizations absent; their cause remains unresolved. Do not infer broad
quality from the bounded same-build parity checks below.

## Build

Install the pinned Gufo README's Linux build prerequisites, including libibverbs
development files, and provide your ROCm installation. No script here changes
system ROCm, the kernel, drivers, boot settings or running services.

```bash
git clone https://github.com/neuhaus/gufo.git gufo-tp2-baseline
git -C gufo-tp2-baseline checkout --detach 0b6978aa1709f2ef863f5407e4ca7261191e9046
# Set BUNDLE to this directory in your Donnerkeule checkout.
BUNDLE=$(realpath /path/to/donnerkeule/docs/patches/gufo-tp2-baseline)
git -C gufo-tp2-baseline apply --check "$BUNDLE/baseline.patch"
git -C gufo-tp2-baseline apply "$BUNDLE/baseline.patch"
ROCM_PATH=/path/to/rocm "$BUNDLE/build-baseline.sh"   "$PWD/gufo-tp2-baseline" "$PWD/gufo-tp2-build"
```

The helper checks the base commit and all eight changed source-file hashes,
then uses Gufo's release preset for gfx1151/TP2. GUFO_DEPS_SYSROOT optionally
selects an isolated development-library sysroot; ordinary installed development
packages do not need it. GUFO_BUILD_JOBS defaults to4.

Set GUFO_BIN to gufo-tp2-build/gufo, then follow
[TP2_TUNING.md](../../TP2_TUNING.md) for model shards, matching provider/runtime,
management Ethernet, token handling, module parameters and the two-node launcher.
The launcher is a configuration helper, not a kernel/module installer.

## Lightweight independent head-gather check

Build the optional probe using the same helper/environment:

```bash
ROCM_PATH=/path/to/rocm "$BUNDLE/build-baseline.sh"   "$PWD/gufo-tp2-baseline" "$PWD/gufo-tp2-build" gufo_head_gather_probe
```

With native RDMA already ready on both machines and runtime library paths set,
run these on their respective hosts (replace HEAD_IP/device names):

```bash
# Rank0 first; start rank1 within30 seconds.
./gufo-tp2-build/src/models/qwen38_flash_next/gufo_head_gather_probe 0 HEAD_IP usb4_rdma0
./gufo-tp2-build/src/models/qwen38_flash_next/gufo_head_gather_probe 1 HEAD_IP usb4_rdma0
```

Each rank checks40 cases against independent CPU row-wise rank0/rank1 bit
concatenation, including one/two rows, awkward widths, signed zero/NaN payloads
and output guards. This uses the real two-host transport, not a mock collective.
It tests copying/ordering, not model accuracy. No model download is required.

## Qualification and identity

Pinned base0b6978a; local qualification executable SHA256
21839bd9d969e52336c00d2bd8e81eaef4e3e44912cb34a777276a06506f42b1.
Hashes are provenance, not a binary download or a promise of reproducible debug
metadata across checkout paths. Formatting passed the shared check on692 C++
files. The patch applies to a clean pinned index and matches the manifest.

Ten same-world request comparisons matched the measured b288378fe4 baseline
outputs and work across TP1/TP2:32K prompt/8 outputs,2K prompt/max128 outputs,
and two concurrent short128-token requests, plus warmup. The brief2K prompt
stopped at96 tokens; this is not a tg128 speed result. Context65536,UD-Q4_K_XL,
MTP/thinking OFF,greedy,cache reads OFF,1GiB RAM cache budget. Transport counters
were unchanged. Four individual phase timing flags remain inconclusive; no
performance improvement or full timing equivalence is claimed.

[Detailed qualification](../../experiments/portable-tp2-baseline.md).
