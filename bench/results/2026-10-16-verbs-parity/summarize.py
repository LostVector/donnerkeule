#!/usr/bin/env python3
"""Retain every round; summarize matched unprofiled cases, excluding warmup."""
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parent
cases = {}
for host in ('fuzzy', 'misty'):
    for path in sorted((ROOT / host).glob('*.log')):
        rows = []
        for line in path.read_text().splitlines():
            if line.startswith('RESULT '):
                row = dict(field.split('=', 1) for field in line.split()[1:])
                row['host'] = host
                rows.append(row)
        if rows:
            cases.setdefault(path.stem, []).extend(rows)
summary = {}
for case, rows in cases.items():
    # A full correctness matrix is not a homogeneous performance case.
    if len({(r['bytes'], r['op']) for r in rows}) != 1:
        continue
    values = [float(r['us_per_message']) for r in rows if int(r['round']) > 0]
    summary[case] = {'median_us': statistics.median(values),
                     'min_us': min(values), 'max_us': max(values), 'rounds': rows}
(ROOT / 'timings.json').write_text(json.dumps(summary, indent=2) + '\n')
for size in (32, 10304, 1048576, 20971584, 33554368):
    print(size, ' '.join(f'{label}={summary[f"{label}-{size}"]["median_us"]:.2f}'
          for label in ('candidate-a', 'host-a', 'candidate-b', 'host-b')
          if f'{label}-{size}' in summary))
