# Maintained Gufo build for this pair

Adopted Gufo revision: `98459477fca57f8bbc6fe3f1156b87f499b65be4`, on
[LostVector/gufo `tp2`](https://github.com/LostVector/gufo/tree/tp2).
Its tree equals the separately built and qualified integration `246a8d11`.
The fork's `main` mirrors gufo-org/gufo at `276dd180`. Neuhaus supplies the
original RDMA lineage; the old source bundles remain available for reproducing
historical measurements.

## Build once and deploy the same executable to both nodes

Use the [Gufo Linux build prerequisites](https://github.com/LostVector/gufo/blob/tp2/README.md#build-from-source)
and your working ROCm compiler/runtime environment. For the native RDMA build:

```bash
git clone --branch tp2 --single-branch https://github.com/LostVector/gufo.git gufo-tp2
cd gufo-tp2
# Replace this with the full adopted revision recorded above.
git checkout 98459477fca57f8bbc6fe3f1156b87f499b65be4
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

## Settings and evidence boundaries

The combined build passed native12-output replay,4cache/9cancellation records,
60 B/C/B controls and C3/C4/staggered completions, plus live owner-read EIO and
peer-process loss followed by separate fresh recoveries. Healthy owner/receiver
row counters match, showing the sharing path was active. Final transport/kernel,
boot/config/topology and idle checks pass. Hosted
[repository](https://github.com/LostVector/gufo/actions/runs/41417033951) and
[Windows packaging/CPU](https://github.com/LostVector/gufo/actions/runs/41417037085)
checks passed for the qualified246 source. Windows TP GPU inference is not covered.

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
adoption commit. Current combined B/C/B output/ordering checks are bounded
qualification, not a statistical throughput comparison. Twelve wider phase
entries and26 changed-work fields remain advisory, including C4 gaps and the
ragged33-token timing mode. No uniform-latency, equivalence, model-quality or new
combined-throughput claim is made. The raw evidence and offline auditors live in
[PLE data](https://github.com/LostVector/gufo/tree/tp2/docs/patches/tp2-ple-owner-data)
and [early-End data](https://github.com/LostVector/gufo/tree/tp2/docs/patches/tp2-early-end-data).

The engine update does not establish that the startup HIP high mode or the SSD
plateau is fixed. Keep those investigations separate from the measured endpoint
improvements and preserve disk-backed n-gram scaling.
