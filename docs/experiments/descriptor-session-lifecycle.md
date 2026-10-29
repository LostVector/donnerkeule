# Descriptor sessions across module reloads

Descriptor/GPU DMA-BUF mode is experimental and is not the native host-WRITE
configuration recommended by the TP2 guide. A ready RDMA device or four ready
rails does not establish that the recorded peer session is current.

## Failure observed and isolated

An uninstrumented Gufo credits test failed its first prefill with WC status12
(retry exhaustion). Snapshots showed EVO still recorded Corsair's previous
`native_host_id` after Corsair reloaded. Each failed host posted zero payload
DMA frames and granted zero credits. Disabling direct posting did not fix it.

The driver deliberately rejects a changed remote host ID with `-ESTALE` in
`tbv_native_control_apply_remote()` in [native_control.c](../../kernel/native_control.c).
Existing raw rings cannot safely accept a restarted peer in place. Our old
per-host unload-and-immediately-load sequence let the first new module bind
the second host's old session. Bypassing the stale-ID check would defeat that
safety boundary.

## Correct reload sequence

Keep management Ethernet active and stop all applications using the RDMA QPs.
Use normal module removal only; an active reference is a reason to stop and
release its owner, never to force removal.

1. Unload `thunderbolt_ibverbs` on **both** hosts before loading either host.
2. Verify `/sys/module/thunderbolt_ibverbs` is absent on both hosts.
3. Run the existing validated load command on each host, with matching
   descriptor options. Do not change driver/kernel/options just to recover IDs.
4. Wait for the expected rails and collect fresh summary/peer snapshots.
5. Check reciprocal current IDs before opening QPs. If they mismatch, recreate
   both sessions; do not edit the IDs or keep benchmarking until a run succeeds.

On each host, collect its snapshot immediately after the reload:

```sh
sudo cat /sys/kernel/debug/thunderbolt_ibverbs/summary \
  /sys/kernel/debug/thunderbolt_ibverbs/peers > descriptor-session.txt
```

Bring the two files to the same machine, then run:

```sh
python3 tools/tbv-check-descriptor-session.py node-a.txt node-b.txt
```

Exit0 means each supplied snapshot records the other snapshot's current local
ID. Exit1 means stale, missing or malformed identities, or a non-descriptor
snapshot. The tool is read-only: it does not load modules or contact peers.
Snapshots are not atomic/live proof; collect them fresh, do not reload afterward,
and still validate actual payloads/ordering before timing. It does not validate
all rail/capability/configuration fields or prove transport correctness.

## Validation and limits

The corrected both-module-absent barrier and fresh reciprocal-ID gate passed
three direct/worker-only/direct starts and15 matching Gufo requests. Driver
counter windows and checked kernel logs were clean; both hosts restored their
approved native baseline. The helper accepts four matching captured pairs and
rejects both stale captured pairs; eight CLI smoke cases cover malformed and
mixed identities. Run them with:

```sh
python3 tools/ci/descriptor-session-smoke.py
```

Credits plus pushed DONE/poll100 measured32–33tok/s uninstrumented decode in that
finite pilot, versus earlier native controls near39tok/s. No randomized
confidence or default adoption follows. Ten strict phase timing flags remain.
A small favorable prefill observation remains eligible for a proper confidence
test; it is not dismissed merely because it is small.

A separate two-direction process-loss test confirmed SIGKILL with a receive WQE
and credits armed, followed by zero QPs/MRs and fresh same-module four-verb8KiB
recovery, including remote-access-error checks. The nonholder had finished its
last send, so that does not qualify blocked-peer timeout, cable unplug,
DMA-in-flight loss, every cancellation path or overnight stability.

This fixes the lab reload lifecycle for the observed startup failure. It is not
a driver data-path patch or proof that every descriptor/credits issue is fixed.
