# Native SEND/WRITE order and retirement regression tests

Run `python3 tools/ci/native-rc-order-tests/run-order.py` and
`python3 tools/ci/native-rc-order-tests/run-retirement.py` from the repository.
They compile actual extracted production helpers under ASan/UBSan, with host
mocks for kernel queues/reference state. The retirement test includes the old
production builder from base4908c75 to reproduce abandoned-frame accounting.
No GPU or network is needed. Simulation does not qualify hardware concurrency,
all RC verbs, disconnect behavior or performance. Live disjoint/overlap/alias
checks and error/retry snapshots are separate gates. The rebuilt unsigned module is byte-identical to the earlier bounded hardware
qualified52C08D90CDFE35DF0026149 artifact. Today's four-rail/fixed-core serving
measurements still use81C6; this is not a new hardware campaign.
