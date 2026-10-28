# Gufo TP2 on dual Strix Halo: tested settings and launch recipe

Measured 2026-10-27 on GMKtec EVO-X2 + Corsair AI Workstation300,
each Ryzen AI Max+395/128GB, one USB4 cable trained at 20Gbit/s per direction.
This records our best-tested **experimental starting profiles**, not a proven
universal optimum. It includes a one-command-per-rank launcher **after the
compatible driver, provider and Gufo are installed**. It is not a fresh-OS
installer; exact benchmark reproduction has unpublished prerequisites below.

## 1. Choose the tested profile

| Setting | `TP_PROFILE=prefill` | `TP_PROFILE=short` |
|---|---|---|
| Model | Qwen3.8-Flash-Next Unsloth UD-Q4_K_XL | Same |
| Context limit |65536|4096|
| CLI prefill chunk |2048|512|
| Sessions |2|2|
| RAM prompt-cache budget |1073741824bytes (1GiB)|Same|
| Speculation / thinking |off / off|Same|
| Sampling |temperature0, seed1|Same|
| TP world size |2|2|
| HTTP bind |127.0.0.1:28530|Same|
| Transport |native registered-host-memory verbs|Same|
| N-gram policy |original disk-backed behavior; no full-table warming|Same|

Use `prefill` for the tested8K/32K workloads; `short` reproduces the earlier
short-agent experiment envelope. We did not isolate context size from chunk
size in a settings sweep, so this does **not** establish2048 as the optimal
chunk, nor a speedup caused by either individual flag. The engine may combine
work into larger actual chunks; log the observed geometry rather than assuming
CLI chunk size equals a kernel's row count. At 32K we observed4096max rows/
8chunks on one node and8192max rows/6chunks on TP2 with identical CLI settings.

Do not extrapolate this to256K context,8sessions, MTP, other quants or
GLM/DeepSeek. Those profiles were not qualified by this investigation.

## 2. Prerequisites and provenance — read before using the numbers

Both nodes need identical model shards, a compatible Gufo TP2 build, the
libibverbs provider matching the driver, and a working native RDMA link.
Gufo must be built for ROCm/gfx1151 with `GUFO_ENABLE_TP2_RDMA=ON`;
a build without that feature cannot use the launcher.
Start with the repository's [installation guide](../README.md#install-from-github-releases)
and the [board notes](STRIX_HALO_BOARDS.md), but distinguish their DMA-BUF/
two-cable experiments from our **one-cable host-window** results. This guide
does not adopt their boot options or DMA-BUF flags. Management/control runs
over Ethernet while USB4 carries tensor exchange. Keep that management path
working; the dummy GID interface below is not a cable IP route.

Exact measured stack:

| Component | Measured revision/identity |
|---|---|
| Linux |7.3.0-strix-test (custom test build, not a generic release recommendation)|
| ROCm/HIP |private user-local7.14.1 install; no system ROCm replacement|
| Gufo base |[neuhaus/gufo](https://github.com/neuhaus/gufo) `0b6978aa1709f2ef863f5407e4ca7261191e9046` **plus private changes**|
| Measured Gufo executable SHA256 |`b288378fe41039f940aed5747c21c0964d9937e0f586e2cef063a2fd087f7599`|
| Adaptive driver private build sourceversion |`BB616A07CF93BF833C36C26`|
| Restore/baseline private build sourceversion |`81C6FF2FBAC473C7AB21844`|

The measured Gufo includes host-staging and one/two-row head-sharding/
half-head-gather changes. The private driver additionally has RC ordering/
retirement fixes and diagnostic probes. **The clean polling branch in this
fork is not that exact full stack.** The Gufo base commit alone does not
produce the measured executable, and the binary hash is provenance, not a
public download. Do not promise our throughput or ordering qualification from
a stock checkout. Public patch/build packaging for those prerequisites is
still needed for a complete from-source one-shot reproduction. This document
and launcher make the **settings** reusable without pretending that gap is closed.

## 3. Driver policy we actually tested

On our native host-window stack:

```text
profile=linux_perf tbnet=prefer_rdma lanes=2
bind_services=1 allocate_rings=1 start_rings=1 negotiate_native=1
enable_tunnels=1 register_verbs=1 roce_netdev=tbv_strix
native_write_striping=1 native_rx_progress=1 native_rx_recheck_ms=1
native_rx_active_poll_us=100
```

The final polling code in this branch interprets100 as a **cap**: below 8 RX
frames, spin for at most 20us; larger bursts may use 100us. Both caps are relative
to worker entry, never a fresh 100us per packet. Default 0 disables active
polling. Apply options through your existing safe module-load procedure,
after applications release their QPs. **Do not mix these with DMA-BUF verbs
options and assume the result was tested.** Our GPU-accessible DMA-BUF mode
was unsupported; GPU staging is part of the measured path.

Dependencies `thunderbolt`, `ib_core`, `ib_uverbs`, `configfs` must be loaded
before the out-of-tree module. Native DMA rings must be available rather than
owned by stock Thunderbolt networking. See the existing install/transport
procedures for that switch and rollback; the launcher below never unloads
modules, changes interfaces or writes boot configuration.

Our GID mapping was `tbv_strix` dummy interface 10.78.0.1/30 on rank0 and
10.78.0.2/30 on rank1; control addresses were separate Ethernet addresses.
Choose your own unused addresses/device and verify `ibv_devices`,
`ibv_devinfo -d usb4_rdma0` and the selected GID/link on **both** machines.
`usb4_rdma0` is an example: enumeration can differ between hosts.

Choose a policy deliberately:

- **Off (`native_rx_active_poll_us=0`)**: conservative default/restore policy.
- **Adaptive cap100 in this branch**: lower-CPU experimental compromise.
- **Full cap100**: add `native_rx_active_poll_full=1` alongside
  `native_rx_active_poll_us=100`. This keeps the full window even for small
  nonempty batches. It never spins on empty entry, extends the deadline per
  packet, or removes the 256-frame batch budget. The default is `0` (adaptive).
  Both flags are load-time options, requiring a safe reload after QPs close.

The historical private full100 build measured about 2.4% higher prefill than
adaptive in their direct comparison, with EVO whole-system CPU averaging 3.77
versus 2.98 logical cores during the **short-decode** test. Those CPU figures
are not prefill-specific or a sum across both hosts. This public selectable
mode restores that polling-window policy, but has not yet been hardware-tested
as this exact build; it does not inherit the private build's qualification.

All experimental policies still have request-latency failures. Our machines
were restored to baseline after testing; no policy is promoted as production-safe.

## 4. Set up once, then launch each rank with one command

On rank0, create a private token and copy it to the same path on rank1:

```bash
install -d -m700 "$HOME/.config/gufo"
(umask 077; openssl rand -hex 32 > "$HOME/.config/gufo/tp2.token")
ssh USER@PEER 'install -d -m700 "$HOME/.config/gufo"'
scp "$HOME/.config/gufo/tp2.token" USER@PEER:.config/gufo/tp2.token
ssh USER@PEER 'chmod 600 "$HOME/.config/gufo/tp2.token"'
```

On **each node**, set its local paths and the same rank0 Ethernet address:

```bash
export GUFO_BIN="$HOME/bin/gufo"    # your compatible TP2-enabled build
export MODEL="$HOME/models/Qwen3.8-Flash-Next-GGUF/UD-Q4_K_XL/Qwen3.8-Flash-Next-UD-Q4_K_XL-00001-of-00004.gguf"
export TP_TOKEN_FILE="$HOME/.config/gufo/tp2.token"
export TP_HEAD=192.168.64.125       # replace with rank0 management address
export TP_DEVICE=usb4_rdma0        # choose the correct local native device
export TP_PROFILE=prefill          # or short
```

All four GGUF shards must be present locally on both machines. Optional,
only if you use a private provider/ROCm install rather than installed libraries:

```bash
export PROVIDER_LIB_DIR="$HOME/rdma-runtime/lib"  # contains libusb4_rdma-rdmav*.so (or .so)
export ROCM_LIB_DIR="$HOME/rocm/lib"
```

From this fork's checkout, start rank1 in one terminal, then rank0 in another:

```bash
# rank1 / worker
./tools/gufo-tp2-profile.sh 1

# rank0 / coordinator
./tools/gufo-tp2-profile.sh 0
```

Both stay in the foreground; Ctrl-C rank0, then ensure rank1 exits before
reloading any module. The launcher sets TP2 bootstrap 28531/control 28532 and
uses the same token; it does not print that token. Rank0's HTTP endpoint is
loopback-only. Confirm readiness through the server logs and:

```bash
curl --fail http://127.0.0.1:28530/v1/models
```

The launcher is the exact recorded argument profile with portable paths,
not a systemd installer. It leaves production services and autostart untouched.
Do not use our machine-specific sudo helper/install scripts on other systems.

## 5. What performance was measured

Same model/binary/high-level settings across world sizes; adaptive polling,
MTP off, context64K, CLI chunk2048. Two starts per world in1/2/2/1 order,
three repetitions/size/start, exact uncached repeated technical-prose prompts:

| Prompt | One EVO tok/s | TP2 tok/s | TP2 gain |
|---|---:|---:|---:|
|2048|1730.8|1754.1|+1.3%|
|8192|1679.0|1852.7|+10.3%|
|32768|1640.2|1818.9|+10.9%|

At 32K, prefill19.98→18.02s. This is a modest scaling gain, not2x. All 36 timed
requests processed their full prompt uncached; 8-token completion hashes agreed
in this fixture. Eight-token agreement does not resolve broader cross-world
numerical/continuation-quality limits. Other prompt distributions and link
speeds are not the same benchmark. Per-start measurements are in
[tp2-prefill-settings.csv](experiments/tp2-prefill-settings.csv).

The separate 4K-context driver A/Bs must not be combined with this table:

- Off→adaptive: prefill1553.30→1678.85tok/s (+8.08%), short decode40.541→40.504
  (unchanged), EVO short-request busy CPU2.870→2.974cores.
- Full 100→adaptive, independent matched round: prefill1671.38→1632.74(-2.31%),
  short decode40.796→40.512, C2aggregate58.461→58.469, EVO busy CPU3.766→2.979
  (-20.9%). CPU is whole-system request-window usage, not driver-only or watts.
- 9/104 and 10/104 strict timing flags remain (>5% AND>3ms). No configuration
  passed all latency gates; no universal p99 improvement or decode win.

Full driver evidence: [adaptive measurements](experiments/native-rx-adaptive.md).
Observed 100–119ms diagnostic gaps overlap n-gram reads and WaitRead/WaitPle on
both off/adaptive, without tracked retry/error increases. The original 424ms
untraced event remains unexplained; do not call it fixed by these settings.

## 6. Qualification checklist for your own pair

1. Record actual kernel/ROCm/Gufo/driver revisions, provider, cable/link speed
   and power/clock settings. More cables/bandwidth are new experiments.
2. Verify host payload ordering/reuse and tracked retry/error counters before
   running the model. A device merely enumerating is not an RDMA correctness test.
3. Compare the exact same prompt/tokenizer/template, cache policy, MTP setting,
   session count and output length. Record **actual** cached/prefill tokens,
   chunk geometry, TTFT, server phase time and completion hashes.
4. Alternate starts and reverse order. Keep first/repeated requests and all
   failed cases; do not silently discard slow sessions or warm the full table.
5. Test real prompts, tool calls, continuation, cancellation and peer recovery
   before adopting a service.2 sessions/AR evidence is not8-session/MTP validation.

The next performance investigation is paired-prefill computation and staging/
communication overlap at8K/32K. Current throughput alone does not prove the
remaining gap is CPU dispatch, GPU compute, transport or memory placement.

Launcher validation: Bash syntax and mocked-executable checks cover both ranks,
both profiles, paths with spaces, ABI-suffixed provider discovery, token not
printed, and invalid inputs. This validates argument construction, not a fresh
public-stack hardware installation. The measured private launcher used the
same argument values; the newly portable wrapper has not been used to claim
new benchmark results.
