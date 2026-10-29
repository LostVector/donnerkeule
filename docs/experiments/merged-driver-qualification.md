# Exact merged driver: bounded two-node hardware qualification

2026-10-29, EVO-X2 + Corsair AI Workstation300, two 40 Gb/s cables/four native rails, Linux 7.3 with the separately qualified Thunderbolt core BDA6ED32433F9503D84E02F. This closes the exact-combined-module hardware-check gap after fork PRs #3, #1 and #2 were merged. It is not all-verbs/DMA-BUF/overnight certification or a new speedup claim.

## Pinned source and settings

Fork main commit `e1e14cb4aa7529ceb00ef3cf8f3ae4486bc36f1e` combines serialized RX progress, RC ordering/TX retirement, optional bounded polling and native TX default 128. The exact tested unsigned module has srcversion `6C607C44DDF7AC1A9A38F61`; checksum and source-build validation are in [module.json](merged-driver-qualification/module.json). Secure Boot hosts signed their local copy with their already enrolled key; no signing key or signed module is redistributed. Hashes describe the tested build, not a promise of identical output under another compiler/kernel.

| Configuration | Native TX window | RX active cap | Full-window flag |
|---|---:|---:|---:|
| Explicit measured-baseline override |32|0|0|
| Fork defaults (active polling off) |128|0|0|
| Adaptive polling |128|100 µs|0|
| Full polling |128|100 µs|1|

All keep native_rx_progress=1, native_rx_recheck_ms=1, all domains and write striping. Adaptive policy may use a shorter window for small batches;100 µs is the cap. The loaded baseline before/after remains81C6 / TX32. Merging did not change either host's boot/autoload module.

## Directly measured checks

- 16 candidate paired transport cases:64,000 completed RTT samples, host and GPU-staged8/16/32/64 KiB, back-to-back and300 µs requested idle. Eight additional before/after baseline-control cases add32,000 samples. [All96,000 RTT quantiles](merged-driver-qualification/latency.csv) are descriptive single-campaign data; they are not statistical model-speedup qualification. GPU-staged buffers are not a DMA-BUF zero-copy certification.
- 32 candidate ordering/partial-overlap/physical-alias cases, including depth 32 / 4 MiB in both directions across all 4 configurations. Additionally, under full 100 µs / TX128, the original depth 128 / 5 MiB overlap and alias patterns pass 2 rounds in each direction:4 extra cases,8 clean endpoint-counter windows. [Deep-case record](merged-driver-qualification/deep-original.json).
- 16 deliberate peer exits and16 fresh successful recoveries. Expected ACK-error/reorder-timeout increments are retained separately from clean windows, never hidden or counted as clean traffic.
- 20 actual Gufo TP2 requests (5 per configuration) match the portable baseline's exact outputs and token/cache work. Flash-Next UD-Q4_K_XL, context 65536, 2 sessions, CLI chunk 2048 / backend 4096, MTP off, original bounded disk-backed ngram policy. No full-table RAM warming. This is functional parity, not broad cache/cancellation/quality qualification.
- No tracked error/retry increases in audited clean windows; no BUG/Oops/panic/refcount flags in the full captured kernel-log window. [Qualification summary](merged-driver-qualification/qualification.json).

Both servers, test processes and candidate QPs stopped. Both hosts were restored normally to81C6 / TX32 with four ready rails; no forced unload, kernel/ROCm/boot change or permanent candidate installation.

## Limits and reproduction

One campaign per configuration cannot clear the earlier strict request-phase timing flags or establish a throughput/latency advantage. E100 storage tails remain a separate known confound. Preserve earlier negative/flagged results. Wider RC/verbs/platform, DMA-BUF, sustained/overnight and model-quality coverage remains open.

The retained workstation controllers are `~/strix-dispatch-investigation/fork-integration/hardware/{module.py,run.py,deep-original.py,analyze.py}` using the previously installed reusable root lab runner. All raw CSVs, per-case logs, snapshots, model JSONLs, signing manifests and restoration inventories remain on the test pair. Public source build/regression commands remain the repository's kernel make target, 11 framework tests and ordering/retirement ASan/UBSan harnesses. This report does not supply a fresh-OS privileged installer.

A wrapper initially routed the first new model phase's two intermediate snapshots into the older native-public results directory. New candidate32 snapshots were preserved under this campaign; routing was corrected before later phases. Historical before/after snapshots were untouched and the historical whole-model-window counter audit was rerun successfully. The lost historical intermediate bytes and provenance are explicitly recorded on the hosts; current qualification uses its own intact before/after windows. No raw current payload/model result was replaced or discarded.
