#!/usr/bin/env python3
"""Median of the six rank/round averages for rounds 1-3, per case."""
import re, statistics, sys
from pathlib import Path
runs = Path(sys.argv[1])
pat = re.compile(r"RESULT round=(\d+) rank=(\d+) op=(\S+) bytes=(\d+) .*us_per_message=([\d.]+)")
rows = {}
for log in sorted(runs.glob("*.log")):
    host, _, case = log.stem.partition("-")
    if host not in ("fuzzy", "misty") or case.count("-") != 2 or not any(case.startswith(t + "-") for t in sys.argv[2:]):
        continue
    for m in pat.finditer(log.read_text()):
        if int(m.group(1)) == 0:
            continue
        rows.setdefault(case, []).append(float(m.group(5)))
for case, values in sorted(rows.items(), key=lambda kv: (kv[0].split("-")[0], kv[0].split("-")[1], int(kv[0].split("-")[2]))):
    print(f"{case:32} n={len(values)} median={statistics.median(values):10.2f} min={min(values):10.2f} max={max(values):10.2f}")
