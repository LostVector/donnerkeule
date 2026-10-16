#!/usr/bin/env python3
"""Check maintained scripts without executing lab or hardware operations."""
import ast
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
count = 0
for directory in ("tools", "packaging", "userspace", "bench"):
    for path in sorted((ROOT / directory).rglob("*")):
        if path.is_relative_to(ROOT / "bench/results"):
            continue
        if path.suffix == ".py":
            ast.parse(path.read_text(), filename=str(path))
        elif path.suffix == ".sh":
            subprocess.run(["bash", "-n", str(path)], check=True)
        else:
            continue
        count += 1
print(f"Script syntax: PASS ({count} scripts)")
