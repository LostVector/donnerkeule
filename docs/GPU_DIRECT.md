# GPU-to-GPU DMA pools

The experimental `gpu_direct=1` mode implements this data path on the native
Donnerkeule rails:

```text
HIP producer kernel -> dedicated HIP allocation -> DMA-BUF
                      | static attachment to each actual NHI DMA device
                      v
                4096-byte NHI TX descriptors
                contiguous blocks on 1..8 native rails
                      | USB4
                preposted NHI RX descriptors
                      v
                dedicated HIP allocation -> HIP consumer kernel
```

There is no CPU mapping or copying of the payload and no host-pinned staging
allocation. CPU work is descriptor submission, rendezvous and completion.
The integrated GPU's allocation and the NHI descriptors refer to the same
backing storage, pinned by static DMA-BUF attachment/mapping. DMA addresses
are obtained separately for each rail's actual `tb_ring_dma_device()`; an
IOVA from one NHI is never reused for another NHI.

This is an **exclusive experimental message transport**, not an implementation
of `ibv_reg_dmabuf_mr()` or arbitrary one-sided RDMA WRITE. The existing verbs
path, including its copy-based write striping, is unchanged when the mode is
off. Both peers must advertise the GPU-direct capability. Mixed direct/verbs
peers are rejected during native negotiation.

## Why a new receive contract is necessary

An NHI receives a packet into its next RX descriptor. It cannot read an rkey
and remote address from a packet and select the tensor's destination like an
RDMA HCA. Donnerkeule's normal receiver parses framed packets in driver-owned
memory and copies their payload into registered MRs. Its previous raw TX
zero-copy path also used those RX buffers, and excludes retryable RC and
striping. Merely adding DMA-BUF registration would leave these copies intact.

The new mode leaves RX empty until the application arms an exact tensor
window. It reuses Donnerkeule's native service discovery, per-cable negotiation,
tunnels and contiguous block striping. A sideband rendezvous names the window,
length and sequence before either side submits data. The payload has no CPU
header, and every frame lands directly in the imported window.

## API and ownership

The fixed-width ABI is in `include/tbv_gpu_direct.h`, implemented in
`kernel/gpu_direct.c`. `/dev/tbv_gpu_direct` requires `CAP_SYS_RAWIO` and permits
one open session per module instance.

1. Allocate distinct, dedicated TX and RX pools with HIP. Verify base and full
   size with `hipMemGetAddressRange`, finish initialization, and export each
   with `hipMemGetHandleForAddressRange(..., hipMemRangeHandleTypeDmaBufFd, 0)`.
   The tester rounds the whole pool to 2 MiB: on the tested runtime a 3 MiB
   HIP range exports a larger BO, which the exact-size importer rejects.
2. `GET_TOPOLOGY` supplies ready rails, link UUIDs, host IDs and HopIDs. Agree
   on one order across the peers using those identities, **not domain numbers**.
   The two cables on fuzzy/misty enumerate in opposite orders.
3. `IMPORT` attaches/maps both objects to every selected rail. Importing
   posts no descriptors. Failed imports unwind maps, attachments, references
   and frame metadata in reverse order and can be retried.
4. The GPU produces TX data and finishes all previous reads of the RX window.
   Complete a system-scope producer release before `SUBMIT_TX`.
5. `PREPARE_RX` posts the selected RX window, split into contiguous blocks.
   Only then publish readiness to the peer. The peer must match the sequence,
   byte count and window offset before sending.
6. `SUBMIT_TX` posts the corresponding TX window. TX remains immutable through
   `WAIT(TX)`. `WAIT(RX)` succeeds only after every expected descriptor completes
   without a length, PDF, CRC or overrun error. It does **not** repost RX.
7. Perform the GPU RX acquire, then launch the consumer directly on the RX
   window. The window remains leased to that consumer until its reads finish;
   only then may another `PREPARE_RX` reuse it.
8. Close the importer before closing exports/freeing HIP pools. A timeout,
   interrupted wait, descriptor error or disconnect aborts the entire session;
   all selected rings are stopped and callbacks drained before unmapping.

Whole objects are limited to 256 MiB per direction; subranges of a larger BO
and aliasing TX/RX BOs are rejected. Transfers use 4096-byte aligned offsets
and lengths, with at most 512 frames per rail per batch. Pad ragged tensor
tails on the GPU; the current API does not carry an unpadded logical length.
An SG entry must contain each entire frame. Unsupported geometry fails instead
of silently copying. There is one active batch per direction and sequences
increase from 1 without gaps.

After any session that submitted descriptors, reload/reconnect **both peers**
before reuse. The leased rails are permanently marked used at close/abort:
headerless frames cannot distinguish a late packet from an earlier session.
There is no retransmission, multiplexing with verbs, persistent GPU doorbell,
or public production-stable ABI here. A no-traffic import probe does not consume
the rails. Physical unplug, GPU reset and IOMMU fault injection remain separate
qualification work.

## GPU coherency boundary

The tester uses a timing-enabled `hipEventReleaseToSystem` marker plus event
synchronization for TX release and, after NHI completion, before launching
the RX consumer. The latter is the implementation-dependent acquire prototype
from strix-rdma, **not a portable HIP API guarantee**. Neither an idle stream
synchronize nor `DMA_BUF_IOCTL_SYNC` alone establishes GPU RX visibility.
Ordinary KFD kernels are not automatically covered by DMA-BUF implicit fences.

The hardware test deliberately reads each RX window on the GPU before arming
DMA, changes the pattern every generation, cycles three windows, then verifies
every word on the GPU. Only a 16-byte mismatch/checksum result reaches the CPU.
Coarse and uncached pools are distinct test arms; passing these tests qualifies
only the tested kernel/runtime/device combination and dispatch pattern.

The static importer follows the kernel's
[DMA-BUF attachment and mapping contract](https://docs.kernel.org/driver-api/dma-buf.html).
It uses `dma_buf_attach()` and `dma_buf_map_attachment_unlocked()`; it does not
manually pin a static attachment or apply CPU DMA sync operations to GPU data.

## Build and run

Build the module with the usual `make KDIR=...`. Sign the resulting `.ko` with
the host's enrolled module-signing key when required. Build the standalone
HIP test using the existing SDK:

```sh
tools/build-gpu-direct.sh /tmp/tbv_gpu_direct
# For a split SDK, set HIPCC, HIP_PATH, HIP_DEVICE_LIB_PATH as necessary.
```

In an exclusive maintenance window, stop users of the verbs/GPU devices and
save the actual loaded module and all its parameters for restoration. Do not
overwrite the installed module. Load the test build on both peers:

```sh
sudo insmod kernel/thunderbolt_ibverbs.ko \
  profile=linux_perf tbnet=prefer_rdma lanes=2 \
  bind_services=1 allocate_rings=1 start_rings=1 \
  negotiate_native=1 enable_tunnels=1 register_verbs=0 gpu_direct=1
```

The paths still use the native driver to negotiate all four rails over two
cables. `native_write_striping` is a verbs option; the direct importer splits
each batch over the rails explicitly selected by its caller.

```sh
# First, on each host: no descriptors, including invalid-import rollback.
sudo /tmp/tbv_gpu_direct --probe --rails 4

# Rank 0, then rank 1 (use a management-network IPv4 address):
sudo /tmp/tbv_gpu_direct --rank 0 --rails 4 --iterations 128
sudo /tmp/tbv_gpu_direct --rank 1 --connect PEER_IPV4 --rails 4 --iterations 128
```

Default sizes are 16 KiB, 64 KiB, 260 KiB (uneven rail division), 1 MiB and
4 MiB. `--uncached` selects the cache-avoiding control. After reloading both
modules, `--rails 1|2|4 --bytes 1048576` measures rail scaling. `--timeout-test`
arms an RX without a sender and checks timeout, draining and rejection of
subsequent TX. It consumes the selected rails and also requires a reload.

`dma_us` measures local submission through both TX and RX completion. It
excludes the preceding TCP readiness handshake and subsequent GPU validation.
`verified_exchange_us` includes GPU production, cache prewarm, control exchanges,
DMA, GPU verification and the final control barrier. Neither is model throughput.
`debugfs` reports `gpu_direct` and per-rail completed frames/bytes; ordinary
verbs WR counters remain zero in this mode.

`WAIT` requeues the existing NHI completion worker after 10 ms without batch
completion, up to the caller's deadline. This handles a completed descriptor
whose interrupt was lost without introducing a concurrent RX callback path.
It never invents a completion, retransmits, or extends the deadline. Debugfs
counts queued workers (`gpu_direct_work_kicks`) and successful waits following
a kick (`gpu_direct_wait_after_kick`). A missing peer still times out and drains
the rings. This is a driver-local workaround; strix-rdma's NHI MSI-X posted-clear
fix is the preferred upstream-level investigation for that notification issue.

## Gufo integration

Gufo now has an opt-in `--tp-transport donnerkeule` Communicator adapter. Its
attention/GDN output projections and MoE epilogues write directly into leased
HIP TX windows; the GPU sums the peer's HIP RX window after NHI completion.
Three 32 MiB windows per direction preserve GPU lifetime across queued work.
TX is cacheable, RX uncached, with a system-release marker before producer-ready
publication. TCP carries only topology and scope/ordinal/shape/readiness metadata.

The two-lane prefill schedule also supports direct leases: publish lane A,
compute lane B, then wait and consume A. A used window is reused only after its
GPU consumer on the same stream. Short prefill and decode keep immediate waits.
This preserves the absence of a staging copy while hiding some transfer time.

The Q8 model smoke matches verbs bit for bit. The standalone verified exchange
still costs more time and process CPU than verbs; the prefill overlap improves
the initial direct integration. Verbs remains the default. Reliable reconnect,
reset recovery, warmed full inference performance and wider model/serving
qualification are still required before promotion.

See the [Gufo integration results and retained logs](../bench/results/2026-10-03-gufo-direct.md)
and the [initial raw hardware report](../bench/results/2026-10-02-gpu-direct.md).

Current checkpoint: [GPU-RDMA / Gufo, October 4, 2026](GPU_RDMA_STATUS_2026-10-04.md).
