# Read-only dual Strix Halo doctor

Run a preflight before comparing TP1/TP2 or investigating a regression:

```bash
python3 tools/dual-strix-doctor.py --peer user@management-host --json doctor.json
```

Python 3.11+ and key-based management SSH are required. The same script is streamed
into the peer's Python process: no remote installation or root login is needed.
Without `--peer`, only the local machine is inspected. The tool makes no fixes,
loads no model, allocates no GPU buffers, changes no service, and never prompts
for sudo. It does not run SMART/admin NVMe commands or read model contents.
Only a requested `--json` output file is written. SSH uses the account's existing
host-key policy; establish and verify management access before running.

The human report distinguishes PASS, FAIL, WARN, UNKNOWN and INFO. Exit status is
0 without failures/warnings/unknowns, 1 for a failed check, 2 for warnings or
unavailable evidence. An unavailable protected file is not counted as healthy.
A stopped model is explicitly unverified, not a rank or model correctness pass.

## What it catches

- Running performance boot arguments and duplicates; differences between hosts;
  effective TTM pages limit and DRM GTT/VRAM capacities. Saved GRUB source lines
  are hints only: the tool never executes configuration shell code and cannot
  certify the generated next-boot menu. Permission gaps are reported.
- Actual remote USB4 routers, rate **per lane × negotiated lanes, per direction**,
  and UUID pairing against the peer's host routers. Four verbs rails do not prove
  two physical cables. RX + TX are never added to label a cable's speed.
- Active native RDMA rails distributed across NHI controllers, loaded module
  source versions, explicit expected polling/TX/striping settings, domain masks
  excluding connected controllers, and userspace provider enumeration.
- Recent bounded kernel errors, memory availability and separate GPU memory
  counters; CPU policies, pressure, and NVMe model/firmware inventory are retained
  in JSON. A snapshot cannot establish sustained clocks, thermal behavior, SSD
  service time, memory bandwidth or the cause of an old stall.
- Running Gufo rank/world size and explicit model, MTP, context, session, chunk,
  cache and sampling parity. Only allowlisted options/environment keys are saved;
  control tokens and full process command lines are not serialized. Model basenames
  are quantization hints, not hash-verified identity. CLI chunk parity does not
  prove actual backend batch geometry. Multiple processes require manual review.
- Other Gufo/profiler/fio/verbs-test processes that can contaminate a comparison.
  `--hash-binaries` optionally compares running executable SHA256; model files are
  never hashed. Without it the tool does not claim binary content identity.

A preflight does not test payload ordering, throughput, safe buffer reuse, peer
recovery or model accuracy. Use the existing bounded correctness/qualification
workloads for those. Cumulative retry/error counters alone do not prove a current
fault. `configured_links: 0` is valid in native mode and is not a missing-cable test.

## Explicit profiles

Supply `--profile profile.json` to compare against an intended configuration.
The portable tool does **not** impose IOMMU-off or a private module on everyone.
Example of our GPU-only Gufo lab policy (NPU use is outside this profile):

```json
{
  "boot_flags": {
    "ttm.pages_limit": "32505856",
    "amd_iommu": "off",
    "amdgpu.gttsize": null,
    "iommu": null
  },
  "physical_links": 2,
  "min_link_gbps": 20,
  "rdma_rails": 4,
  "driver_parameters": {
    "lanes": "2",
    "native_write_striping": "Y",
    "native_rx_progress": "Y",
    "native_rx_recheck_ms": "1",
    "native_tx_max_inflight": "32"
  },
  "min_available_gib": 4
}
```

`null` means the argument must be absent. `amdgpu.gttsize=0` is not removal.
`amdgpu.gttsize` is deprecated but still influences some kernel paths: its removal
was capacity-validated on each of our hosts with `ttm.pages_limit` unchanged.
Both retained the exact 124 GiB HIP total. Reported GTT differed by 4/8 KiB across
reboots; this is not a performance equivalence study. Different BIOS VRAM
reservations can still produce different DRM GTT capacities with matching flags.

Two current lab cables each negotiate 20 Gb/s per direction (10 Gb/s/lane × 2).
Historical 40 Gb/s link observations do not override current negotiated sysfs
readings. Change `min_link_gbps` for the hardware/configuration actually required;
there is no universal 40 Gb/s-per-cable success assertion.

Optional profile fields:

- `kernel`: exact running kernel release expected.
- `module_srcversions`: object mapping module names to qualified loaded versions.
- `runtime`: explicit `ibv_devices`, `LD_LIBRARY_PATH`, and `RDMAV_DRIVERS` paths
  for the installed provider. These apply only to the enumeration subprocess.
- `readback_helper`: explicit opt-in to `sudo -n /absolute/root-owned-helper snapshot`.
  The existing lab helper is checked for root ownership and no group/other write
  permission. Only its read-only snapshot operation is called; no install, reload
  or parameter update. Without permission the counter check is UNKNOWN. The tool
  itself does not require this helper or create sudo policy.

Profiles are trusted local configuration. Keep private paths, private module
hashes and lab snapshots out of public evidence unless deliberately publishing
those measurements. The generic doctor has no machine-specific hashes.

## Tests

```bash
python3 tools/ci/dual-strix-doctor-smoke.py
```

Hardware-independent scenarios cover asymmetric boot flags, duplicates, legacy
GTT, one missing cable despite four rails, a single-controller rail layout,
wrong-peer UUIDs, excluded native domains, unknown negotiated rate, missing
provider devices, permission-limited logs, secret redaction, reversed/duplicate
TP ranks and different model quants. They do not qualify a kernel or transport.
The driver CI smoke runner includes this test.
