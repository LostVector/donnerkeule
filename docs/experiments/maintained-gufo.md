# Maintained Gufo build for this pair

Adopted production revision: `9c7157201e8aa30cbfc51ddf3595a3dea13b196a`, on
[LostVector/gufo `tp2`](https://github.com/LostVector/gufo/tree/tp2).
The branch additionally contains documentation; its production sources match that
qualified revision. Fork `main` mirrors gufo-org/gufo at `773a1641`.
Neuhaus supplies the original RDMA lineage. The earlier combined adoption
`98459477` (tree equal to qualified `246a8d11`) and its measured gains remain
historical records; they are not relabeled as new measurements.

The [current intake record](https://github.com/LostVector/gufo/blob/tp2/docs/patches/tp2-adoption/upstream-773a1641.md)
contains the exact executable hash, commands, raw evidence, actual32K continuation
coverage and retained inconclusive startup timings. This is a correctness/upstream
sync, not a newly established speedup. Q8 reuse forkPR14/upstreamPR958 remain
separate drafts and are not included.

## Build once and deploy the same executable to both nodes

Use the [Gufo Linux build prerequisites](https://github.com/LostVector/gufo/blob/tp2/README.md#build-from-source)
and your working ROCm compiler/runtime environment. For the native RDMA build:

```bash
git clone --branch tp2 --single-branch https://github.com/LostVector/gufo.git gufo-tp2
cd gufo-tp2
# Replace this with the full adopted revision recorded above.
git checkout 9c7157201e8aa30cbfc51ddf3595a3dea13b196a
cmake --preset release -DGUFO_ENABLE_TP2_RDMA=ON \
  -DBUILD_TESTING=OFF -DGUFO_BUILD_TOOLS=OFF -DCMAKE_HIP_ARCHITECTURES=gfx1151
cmake --build --preset release --parallel 4
./build/release/gufo --version
sha256sum ./build/release/gufo
```

The official release preset uses RelWithDebInfo. Keep that preset when comparing
results; do not silently substitute a different build type or testing-enabled
binary. Our qualification used private ROCm7.14.1/Clang23 and the existing
libibverbs/provider environment. Gufo's Nix package has a separately pinned ROCm
stack; this document does not claim timings for that stack or install ROCm.

Deploy the same executable, matching runtime/provider and identical model files
to both hosts. Check the executable hash and version on each. The shared PLE-read
protocol uses native wire22; an older wire21 peer is rejected during startup.
Upgrade and restart the pair together. The driver module/provider and RX polling
settings were unchanged for this Gufo qualification.

## What was incorporated

- [Gufo PR12](https://github.com/LostVector/gufo/pull/12): rank1 reads eligible PLE
  table rows once and shares their exact bits with rank0. Staging is preallocated,
  shape/row metadata is checked and GPU completion protects buffer reuse. Wider
  passes retain the existing local-read path. This does not warm the full table.
- [Gufo PR13](https://github.com/LostVector/gufo/pull/13): queue a finished request's
  End instruction after its local model/cache work and before the next scheduler
  turn. The HTTP consumer still waits for the peer verdict and reused-state
  dependencies. This reduces completion tails; it is not a faster-token claim.

Both behaviors are part of the maintained engine; no new tuning flag is needed.
Canonical773 also brings checkpoint device-headroom fallback and returns local
prefill scratch to4,096rows. Native TP already capped its trunk batches to fit the
exchange window, so the tested TP2 chunk setting remains2,048. Distributed HTTP
continues to use separate mirrored captures; no new in-pass TP capability or
driver setting is enabled.


## Settings and evidence boundaries

The original combined984/246 build passed native12-output replay,4cache/9cancellation records,
60 B/C/B controls and C3/C4/staggered completions, plus live owner-read EIO and
peer-process loss followed by separate fresh recoveries. Healthy owner/receiver
row counters match, showing the sharing path was active. Final transport/kernel,
boot/config/topology and idle checks pass. Hosted
[repository](https://github.com/LostVector/gufo/actions/runs/41417033951) and
[Windows packaging/CPU](https://github.com/LostVector/gufo/actions/runs/41417037085)
checks passed for the qualified246 source. Windows TP GPU inference is not covered.

Current9c independently passes production release/HIP operators, fresh native12
outputs/cache4/cancellation9 and matched94/9c/94 C3/C4/ragged60records. Native
checkpoint assertions pass at short/resumed boundaries and at an actual32,768-token
boundary in65,536-capacity sessions, AR/MTP, including asymmetric process-reported
memory headroom. Single-node snapshot/adapter in-pass headroom tests also pass.
[Current repository CI](https://github.com/LostVector/gufo/actions/runs/41453594920)
and [current Windows packaging/CPU CI](https://github.com/LostVector/gufo/actions/runs/41453598440)
pass on exact9c. Windows TP GPU inference is not covered by the fork workflow.

The new wider comparison retains14phase entries, chiefly first-C3 startup gaps.
Two bounded source traces investigate them; the final first-four-cycle comparison
matches all actual plans/chains/packed rows on both nodes. Its first target-forward
wall time is56.45/57.38/59.65ms (baseline/integration/baseline). This is combined
host/GPU/communication/wait time, not pure dispatch or GPU compute. It does not
clear the original flags or establish timing equivalence. Timings remain explicitly
inconclusive/advisory; the canonical correctness sync is adopted without a speed
claim. See the linked current record for every raw result, failure and limitation.


These runs used Flash-Next UD-Q4_K_XL plus shared-Q8_0 MTP, greedy/thinking off,
65,536 context capacity, two slots (four in the wider test), prefill chunk2,048,
1GiB cache, driver81C6/TX32 with active pollingoff, and four rails with crossed
40+20Gb/s cable links. Context capacity is not prompt length. The current link
configuration does not replace the older dual40Gb/s bandwidth measurements.
Both nodes used `ttm.pages_limit=32505856 amd_iommu=off`, legacy gttsize disabled.
These are recorded existing settings; no kernel/boot/driver change accompanies
this Gufo update.

The one-shot launcher's `prefill` and `short` profiles remain **AR**, matching its
older measurement tables. To reproduce MTP behavior, use the
[Gufo TP2 server commands](https://github.com/LostVector/gufo/blob/tp2/docs/models/qwen3.8-flash-next/TP2.md#run)
with `--speculative mtp --mtp-model PATH` on both ranks and the matching settings
above. Confirm actual draft activity rather than inferring it from the model name.

For a copyable MTP launch after setting the shared paths/token/provider as in
this guide, use the same command on each machine and change only `TP_RANK`:

```bash
export TP_RANK=0  # use 1 on the worker
export MTP_MODEL="$HOME/models/Qwen3.8-Flash-Next-GGUF/MTP/mtp-Qwen3.8-Flash-Next-shared-Q8_0.gguf"
if [[ -n ${ROCM_LIB_DIR:-} ]]; then
  export LD_LIBRARY_PATH="$ROCM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [[ -n ${PROVIDER_LIB_DIR:-} ]]; then
  export RDMAV_DRIVERS="$PROVIDER_LIB_DIR/libusb4_rdma"
  export LD_LIBRARY_PATH="$PROVIDER_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
"$GUFO_BIN" serve llm --model "$MODEL" --served-model-name flash-next-tp-test \
  --speculative mtp --mtp-model "$MTP_MODEL" --context 65536 --sessions 2 \
  --prefill-chunk 2048 --cache-ram-bytes 1073741824 --think off \
  --host 127.0.0.1 --port 28530 --temperature 0 --seed 1 \
  --tp-world-size 2 --tp-rank "$TP_RANK" --tp-bootstrap-host "$TP_HEAD" \
  --tp-bootstrap-port 28531 --tp-control-port 28532 \
  --tp-control-token "$(cat "$TP_TOKEN_FILE")" \
  --tp-rdma-device "${TP_DEVICE:-usb4_rdma0}"
```

The standalone PLE study supports +0.699% first-C1 server decode and +1.940%
short-C2 whole-wave rate at its recorded source/history. The standalone early-End
study supports a completion-tail reduction41.974→0.459ms across ten matched pairs.
These results belong to their original measured revisions, not this combined
adoption commit. The original combined B/C/B output/ordering checks are bounded
qualification, not a statistical throughput comparison. Twelve wider phase
entries and26 changed-work fields remain advisory, including C4 gaps and the
ragged33-token timing mode. No uniform-latency, equivalence, model-quality or new
combined-throughput claim is made. The raw evidence and offline auditors live in
[PLE data](https://github.com/LostVector/gufo/tree/tp2/docs/patches/tp2-ple-owner-data)
and [early-End data](https://github.com/LostVector/gufo/tree/tp2/docs/patches/tp2-early-end-data).

The engine update does not establish that the startup HIP high mode or the SSD
plateau is fixed. Keep those investigations separate from the measured endpoint
improvements and preserve disk-backed n-gram scaling.
