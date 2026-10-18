# Driver verbs / GPU parity work, 16 October 2026

See [the report](../2026-10-16-verbs-parity.md). Raw logs retain warmup,
all rounds, both ranks and rejected candidates. `summarize.py` writes
`timings.json`; it excludes only round 0 from homogeneous performance cases.
Ranks share a transfer and are not independent repetitions.

`task-changes.patch` isolates this task from the pre-existing uncommitted
GPU implementation. `final-source-sha256.json` identifies the final sources.
`verifier-used.hip` is the preserved source of the same old verifier binary
used throughout; the newer optional userspace profiling additions were not
used. Kernel tracing used the current module instrumentation.

`profile-small-report` retains the initial ambiguous per-link rail labels.
`profile-small-final-report` fixes those labels but retains a cold-start
watchdog and retry. `profile-large-final-report` also verifies one aggregate
posting interval per rail despite interleaved chunks. Negative trace statuses
from deliberate invalid-WR checks are retained; these diagnostic captures
are not the unprofiled timing cohorts.

Original loaded modules and all parameters were restored on both hosts,
with build IDs checked and unchanged kernel taint. No installed module or
library was replaced. Application containers removed themselves on stop;
restoring those workloads still needs their original launch command.
