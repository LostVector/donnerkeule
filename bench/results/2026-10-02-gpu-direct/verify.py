#!/usr/bin/env python3
"""Check retained GPU-test totals and per-rail counters against test shapes."""
from pathlib import Path
import re

root = Path(__file__).resolve().parent
matrix = [16384, 65536, 266240, 1048576, 4194304]
cases = {
    "watchdog-coarse": (matrix, 256, 4),
    "one-rail": ([1048576], 128, 1),
    "two-rails": ([1048576], 128, 2),
    "watchdog-uncached": (matrix, 128, 4),
    "completion-soak": ([1048576], 4096, 4),
}
total_frames = total_exchanges = 0
for case, (sizes, iterations, rails) in cases.items():
    frames = sum(sizes) // 4096 * iterations
    total_frames += frames
    total_exchanges += len(sizes) * iterations
    expected_rails = [
        sum((((n // 4096) * (i + 1) + rails - 1) // rails -
             ((n // 4096) * i + rails - 1) // rails) for n in sizes) * iterations
        for i in range(rails)
    ] + [0] * (4 - rails)
    for host in ("fuzzy", "misty"):
        log = (root / host / f"{case}.log").read_text()
        rows = re.findall(r"^PASS rank=(\d) bytes=(\d+) exchanges=(\d+) rails=(\d+) bad_words=(\d+)", log, re.M)
        assert [tuple(map(int, row)) for row in rows] == [
            (int(host == "misty"), size, iterations, rails, 0) for size in sizes
        ], (host, case, "GPU verification")
        counters = dict(re.findall(r"^(\w+): (\d+)$", (root / host / f"{case}.summary").read_text(), re.M))
        for key in ("data_tx_posted", "data_tx_completed", "data_rx_completed"):
            assert int(counters[key]) == frames, (host, case, key)
        for key in ("data_tx_errors", "data_tx_canceled", "data_rx_canceled",
                    "data_rx_bad_frame", "data_wr_copied"):
            assert int(counters[key]) == 0, (host, case, key)
        peers = (root / host / f"{case}.peers").read_text()
        for key in ("data_tx_posted", "data_tx_completed", "data_rx_completed"):
            actual = sorted(map(int, re.findall(rf"\b{key}=(\d+)", peers)))
            assert actual == sorted(expected_rails), (host, case, key, actual)
    print(f"PASS {case}: {frames:,} frames/direction; rails={expected_rails}")
print(f"PASS total: {total_exchanges:,} bidirectional exchanges; "
      f"{total_frames:,} frames and {total_frames * 4096:,} bytes per direction; "
      f"{total_frames * 8192:,} GPU-verified bytes across both directions")
