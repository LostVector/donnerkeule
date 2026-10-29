# Experimental Gufo TP2 peer-add / HC fusion source

This is an optional source layer over the sibling gufo-tp2-baseline bundle.
It is **not part of the recommended baseline or a production-qualified default**.
An independent eight-pair randomized experiment on the original candidate found
+1.73% uncached32K prefill throughput, exact two-sided p=0.0078125. Cold/decode/C2
qualification remains inconclusive. See ../../experiments/hc-fusion-confidence.md.

The four changed files match the originally measured source exactly after the
same clang-format21.1.8 canonicalization. The measured executable hash and source
hashes are in source-manifest.json. Formatting/build-path changes can change an
ELF; do not claim a freshly built executable inherits the exact measured timing.

## Apply and build in an isolated checkout

```bash
git clone https://github.com/neuhaus/gufo.git gufo-hc-experiment
git -C gufo-hc-experiment checkout --detach 0b6978aa1709f2ef863f5407e4ca7261191e9046
# Set this to this directory in the Donnerkeule fork checkout.
FUSION_BUNDLE=/path/to/docs/patches/gufo-hc-fusion
git -C gufo-hc-experiment apply --check "$FUSION_BUNDLE/../gufo-tp2-baseline/baseline.patch"
git -C gufo-hc-experiment apply "$FUSION_BUNDLE/../gufo-tp2-baseline/baseline.patch"
git -C gufo-hc-experiment apply --check "$FUSION_BUNDLE/hc-fusion.patch"
git -C gufo-hc-experiment apply "$FUSION_BUNDLE/hc-fusion.patch"
ROCM_PATH=/path/to/rocm bash "$FUSION_BUNDLE/build-fused.sh" \
  "$PWD/gufo-hc-experiment" "$PWD/gufo-hc-build"
```

Use the baseline bundle's dependency/runtime instructions. The helper checks
all ten relevant source-file hashes and uses the same release preset/gfx1151.
GUFO_DEPS_SYSROOT optionally selects an isolated development-library sysroot.
No installation, driver loading, kernel change or service update is performed.
Keep standard and candidate binaries separately; never replace a live server.

The patch preserves the rounded local+peer addition before HC operations and
existing fallbacks. Eligible prefill stream ordering consumes the peer before
its window is reused. Independent component cases and actual two-rank traces
exist for the measured source; finite coverage is not universal correctness.
Ordinary small-row decode does not establish a decode gain from this mechanism.

## Packaged rebuild checks

Release build and the shared692-file formatting check pass. Five TP2 request
comparisons against the originally measured fusion executable match outputs/work;
25 extra work checks pass and four captured driver-counter windows are clean.
The parity/timing summary is in packaged-model-parity.json. This is bounded
functional coverage, not statistical timing equivalence or a clearance of the
older cold/decode/C2 gates.70/71 embedded AMDGPU text objects match byte-for-byte;
the differing reference object is the previously identified unsupported DeepSeek
MMQ diagnostic. No complete metadata/descriptor equivalence claim is made.
