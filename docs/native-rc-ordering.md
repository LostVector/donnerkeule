# Native host RC SEND/WRITE ordering and TX retirement

An accepted depth128 overlapping-write workload could publish all completions
while final memory still contained an older write. Fragment placement occurred
before message PSN retirement, so CQ ordering alone could not preserve remote
write ordering, especially through aliased registered memory.

This patch keeps one native host RC SEND/WRITE in flight per QP until its remote
ACK retires it. Later accepted WRs retain source/SQ ownership and are dispatched
by a per-QP worker. Rails within a write and separate QPs remain parallel. Idle
QP submission stays inline. Caller ownership is acquired before publishing a
deferred context; close cancels the worker before flushing. A no-post ENOMEM
retries with workqueue backoff; fatal submission failure poisons the QP.

A shared frame-retirement helper accounts for both DMA completions and abandoned
builder frames. The final retirement arms ACK/retry progress even when no future
DMA callback remains. The frame-window default remains32; polling, wire format,
Apple and DMA-BUF paths are unchanged. This does not claim all RC verbs/fences
have been audited or that one-QP serialization is the final performance design.

The source is the frozen native-ordering-and-retirement patch used in the private
hardware stack. Eleven critical helpers exactly match private81C6 source; its
remaining ibdev.c delta comprises three diagnostic trace spans. The clean rebuild's entire unsigned module is also byte-identical to the earlier
qualified52C08D90CDFE35DF0026149 artifact (SHA256
70cb751314c1e32f9be4f48de23437f7b626d0847a64014e87c1aab7f76aae01).
Its recorded hardware gate passed7 initial cases,24 extended cases,4 expected
peer exits with4 fresh reconnections and91 matching model output/work checks.
That earlier bounded qualification is distinct from a new test of this clean
build on today's four-rail/fixed-core setup; current serving still uses81C6.

Production-helper ASan/UBSan regression tests are in tools/ci/native-rc-order-tests.
Kernel build and inherited CI must pass before deployment. Qualification should
then repeat disjoint/overlap/alias, reuse, peer disconnect and actual inference,
retaining all counter/timing failures and restoring the pinned baseline.

This correctness prerequisite should be reviewed separately from RX polling and
TX128 throughput tuning. The Linux7.3 Thunderbolt XDomain lifetime fix is another
separate kernel-core prerequisite; this ibdev patch does not address it.
