#!/usr/bin/env python3
"""CLI checks of stale/partial descriptor identity snapshots; no hardware claim."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/tbv-check-descriptor-session.py"
A, B, OLD = "1" * 32, "2" * 32, "3" * 32

def snapshot(local, remote):
    return f"native_host_id: {local}\ndmabuf_verbs: 1\npeer 1 remote_host={remote}\npeer 2 remote_host={remote}\n"

with tempfile.TemporaryDirectory(prefix="tbv-session-check-") as directory:
    paths = [Path(directory) / name for name in ["a", "b"]]
    cases = [
        (snapshot(A, B), snapshot(B, A), True),
        (snapshot(A, B) + "peer 3 remote_host=malformed\n", snapshot(B, A), False),
        (snapshot(A, B) + "native_host_id: malformed\n", snapshot(B, A), False),
        (snapshot(A, B) + "dmabuf_verbs: malformed\n", snapshot(B, A), False),
        (snapshot(A, A), snapshot(A, A), False),
        (snapshot(A, OLD), snapshot(B, A), False),
        (snapshot(A, B) + f"peer 3 remote_host={OLD}\n", snapshot(B, A), False),
        (snapshot(A, B).replace("dmabuf_verbs: 1", "dmabuf_verbs: 0"), snapshot(B, A), False),
        (f"native_host_id: {A}\ndmabuf_verbs: 1\n", snapshot(B, A), False),
        (snapshot(A, B) + f"native_host_id: {OLD}\n", snapshot(B, A), False),
        (snapshot("0" * 32, B), snapshot(B, A), False),
    ]
    for left, right, valid in cases:
        paths[0].write_text(left)
        paths[1].write_text(right)
        result = subprocess.run([sys.executable, str(TOOL), *map(str, paths)], text=True, capture_output=True)
        assert result.returncode == (0 if valid else 1), result
        assert json.loads(result.stdout)["reciprocal_ids_match"] == valid
print("PASS: 11 descriptor session CLI cases")
