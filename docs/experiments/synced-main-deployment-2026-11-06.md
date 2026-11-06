# Synced main deployed on the Strix Halo pair

On November 6, the EVO-X2 and Corsair were both moved to the driver built from
fork main `3bbb3ee8da058bfe354ad285a6eeebeb6af0e357`. This incorporates Neuhaus
main `d075be0`, including the 248 MiB native message limit, and our merged
CRC/retry, ordering and progress fixes. Both peers need the updated module for
messages above the older 64 MiB cap.

The lab's Linux 7.3.0-strix-test build has sourceversion
`B6C332F34D27E163651EE38`; the unsigned image SHA256 is
`e9c5a5fb51f1ff8b6a01888573a54d3c84c4c5665cb068a93b5b2c4756918f23`.
Host copies are signed with existing enrolled keys where required. These hashes
identify this build, not a portable replacement for checking kernel compatibility.

The deployed profile explicitly uses TX depth 32, RX progress enabled with a
1 ms recheck, active polling 0, both physical cables and four native rails.
The cable readings remain crossed 40+20 Gb/s per direction. Existing reload and
post-reboot initializer metadata now select this build. The previous `844E`
recovery build remains available for explicit rollback; normal test cleanup must
restore the current baseline. No kernel, ROCm or boot-policy change was needed.

## Bounded validation

Twenty host-buffer cases completed with both processes exiting successfully:

| Case, each direction | Rounds | Queue depth |
|---|---:|---:|
| 1 MiB | 64 | 1 |
| 64 MiB + 8 bytes | 8 | 1 |
| 169,082,880 bytes, representing a large Gufo partial | 8 | 1 |
| 200 MiB | 8 | 1 |
| 248 MiB | 8 | 1 |
| 19,630,144 bytes, the earlier failing transfer size | 512 | 1 |
| 1 MiB, disjoint / partial overlap / overlap / physical alias | 32 each | 8 |

Fixed registered buffers are retained until local completion and peer validation.
Disjoint cases verify all words and sequence identity; overlap/alias cases verify
the final region/last writer rather than every overwritten intermediate payload.
These are correctness checks, not transport bandwidth or latency measurements.
The earlier integrity checker changed only its permitted size bound, and its
stale-sequence/corruption self-check still passes.

An unchanged DFlash Release binary then completed six short greedy/sampled/cache
replay requests and eight fixed-TG256 code/prose requests on actual TP2. Full code
and seeded prose outputs match the frozen pre-lookup DFlash control. This is not
a new AR-parity or model-quality study. Context capacity was 8,192, two session
slots with one stream, chunk 512, Q4XL target/shared-Q8 donor and Q8 DFlash.
Median code decode was 125.20 tok/s and prose 43.05 tok/s. These are single-history
observations, not evidence of a driver speedup or of larger engine prefill chunks.

During host tests, two CRC-marked completions were rejected and recovered with
one WR retransmission. Inference rejected and recovered four more, with two WR
retransmissions. There were no new malformed-header, QP-error or retry-exhaustion
counter increments. The physical source of CRC errors remains unresolved; the
driver recovers from them rather than claiming to prevent them.

The ordinary baseline reload was exercised after the workloads. Both hosts kept
the new image and TX32, with four registered native devices and zero module
references. Test servers were stopped. This does not establish overnight,
all-verbs, headerless RAW recovery or full kernel/DMA race qualification.

The source's [25-group CI suite](../TESTING.md) and distribution builds passed
before deployment. The new fault suite covers 11 selected production functions;
its scoped percentages do not imply whole-driver coverage.

The local reproduction controller, image/signing manifests, snapshots, full-word
checker, raw responses, counter deltas and rollback records are retained in
`driver-deploy-2026-11-06/` under the investigation workspace. No new upstream PR
or default Gufo/model change was part of this deployment.
