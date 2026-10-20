#!/usr/bin/env python3
"""Catch module header filtering and packaged provider source drift."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
module = (ROOT / "nix/module.nix").read_text()
entries = re.search(r"sourceEntries = \[([^]]+)\]", module)
assert entries, "Missing module source entry list"
included = set(re.findall(r'"([^"]+)"', entries[1]))
for directory in ("kernel", "proto"):
    assert directory in included, f"Module source omits {directory}"
    for source in (ROOT / directory).rglob("*"):
        if source.suffix not in (".c", ".h"):
            continue
        for name in re.findall(r'^#include "([^"]+)"', source.read_text(), re.M):
            header = source.parent / name
            if not header.exists():
                header = ROOT / name
            assert header.exists(), f"Missing header {name} in {source}"
            assert header.resolve().relative_to(ROOT).parts[0] in included, name

patches = ROOT / "packaging/rdma-core-patches"
series = (patches / "series").read_text().splitlines()
assert len(series) == len(set(series)) == 3, "Expected three distinct provider patches"
assert set(series) == {path.name for path in patches.glob("*.patch")}
with tempfile.TemporaryDirectory(prefix="tbv-provider-source-") as directory:
    # The first patch creates the entire provider; no upstream checkout needed.
    subprocess.run(["git", "apply", str(patches / series[0])], cwd=directory, check=True)
    packaged = Path(directory) / "providers/usb4_rdma"
    provider = ROOT / "userspace/usb4_rdma"
    assert {p.name for p in packaged.iterdir()} == {p.name for p in provider.iterdir()}
    for path in provider.iterdir():
        assert path.read_bytes() == (packaged / path.name).read_bytes(), f"Stale provider patch: {path.name}"
print("Source layout: PASS (module headers, patch series, provider bytes)")
