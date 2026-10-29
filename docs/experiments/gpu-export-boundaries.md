# Verify dedicated GPU DMA-BUF exports

The GPU verifier requires dedicated pools and registers subranges at exporter offset4096. HIP allocation bounds alone do not prove the exported DMA-BUF has the same bounds: on ROCm7.14.1/gfx1151, two256KiB uncached HIP allocations exported the same2MiB object. Registering both at FD offset0 in a separate exchange benchmark placed DMA into the wrong suballocation. Using HSA's returned offsets0/262144 fixed SEND/WRITE at8–64KiB in both directions without a driver change.

The shipped verifier uses98MiB pools; that failure was not reproduced with these pools. It now additionally checks the standard DMA-BUF object size through `lseek(fd,0,SEEK_END)` and fails before registration if it differs from the required dedicated allocation. Supporting arbitrary suballocations would instead require carrying the exporter-returned offset through registrations and import probes. No offset is guessed and no HSA dependency is added here.

## Validation

On two Ryzen AI Max+395/gfx1151 hosts, Linux7.3.0-strix-test, private ROCm7.14.1:

- Standard verifier builds with `-Wall -Wextra -Werror` through `tools/build-gpu-verbs.sh`.
- A GPU allocation test using the modified Pool rejects the shared256KiB export and accepts the dedicated98MiB export on both hosts.
- Exact merged module6C607C44DDF7AC1A9A38F61, descriptor mode with `dmabuf_poll_us=100`, two40Gb/s cables: paired verifier passes8KiB, depth3, two iterations and two fresh MR/QP rounds for GPU signaled, GPU remote-error and host remote-error cases.
- Each case covers SEND, SEND_IMM, WRITE_IMM and WRITE with full GPU payload/guard verification. GPU cases pass eight invalid-import checks and invalid GPU SGE/length/foreign-PD checks per round. Error cases verify REM_ACCESS_ERR/WR_FLUSH_ERR and then recreate QPs.
- Both hosts restore their approved prior modules; no checked kernel warning/crash signatures.

Build with the existing script, specifying HIPCC/HIP_PATH and VERBS_INCLUDE/VERBS_LIB when using a private runtime/provider. For each paired case, start rank0 with:

```sh
./tbv_gpu_verbs --device DEVICE --gid-index 1 --port 29635 \
  --bytes 8192 --iterations 2 --rounds 2 --depth 3 --signal-all
```

Start the peer with the same options plus `--host HEAD_IP`. Repeat with `--remote-error` in place of `--signal-all`, then with `--host-mr --remote-error`. Existing driver setup and provider environment remain prerequisites; these commands do not configure or load a module.

This is bounded correctness coverage, not a full-size matrix, overnight or performance qualification. The change affects the verifier only; no driver or production Gufo behavior changes.
