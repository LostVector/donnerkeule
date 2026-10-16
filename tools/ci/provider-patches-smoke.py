#!/usr/bin/env python3
"""Check directory/archive regeneration on a small upstream-layout fixture."""
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from smoke_support import ROOT

with tempfile.TemporaryDirectory(prefix="tbv-patch-regen-") as directory:
    work = Path(directory)
    repo = work / "repo"
    (repo / "packaging/rdma-core-patches").mkdir(parents=True)
    shutil.copyfile(ROOT / "packaging/regen-rdma-core-patches.sh", repo / "packaging/regen-rdma-core-patches.sh")
    shutil.copyfile(ROOT / "packaging/test-rdma-patches.sh", repo / "packaging/test-rdma-patches.sh")
    shutil.copyfile(ROOT / "packaging/rdma-core-patches/series", repo / "packaging/rdma-core-patches/series")
    shutil.copytree(ROOT / "userspace/usb4_rdma", repo / "userspace/usb4_rdma")
    upstream = work / "upstream"
    (upstream / "libibverbs").mkdir(parents=True)
    (upstream / "CMakeLists.txt").write_text("add_subdirectory(providers/siw)\n")
    (upstream / "libibverbs/verbs.h").write_text("extern const struct verbs_device_ops verbs_provider_siw;\n")
    archive = work / "upstream.tar.gz"
    with tarfile.open(archive, "w:gz") as tar:
        tar.add(upstream, arcname="rdma-core-fixture")
    # Inspect the real host staging path without starting a distro container.
    container = work / "inspect-container-stage"
    container.write_text('''#!/bin/sh
set -eu
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-v" ]; then
        stage=${2%:/work:ro}
        cmp "$stage/upstream/CMakeLists.txt" "$TEST_UPSTREAM/CMakeLists.txt"
        cmp "$stage/upstream/libibverbs/verbs.h" "$TEST_UPSTREAM/libibverbs/verbs.h"
        cmp "$stage/patches/series" "$TEST_REPO/packaging/rdma-core-patches/series"
        cmp "$stage/provider/usb4_rdma.c" "$TEST_REPO/userspace/usb4_rdma/usb4_rdma.c"
        exit 0
    fi
    shift
done
exit 1
''')
    container.chmod(0o755)
    for index, source in enumerate((upstream, archive)):
        subprocess.run(["bash", str(repo / "packaging/regen-rdma-core-patches.sh")],
                       env={**os.environ, "RDMA_SRC": str(source)}, check=True,
                       stdout=subprocess.DEVNULL)
        patched = work / f"patched-{index}"
        shutil.copytree(upstream, patched)
        patches = repo / "packaging/rdma-core-patches"
        for name in (patches / "series").read_text().splitlines():
            subprocess.run(["git", "apply", str(patches / name)], cwd=patched, check=True)
        for path in (ROOT / "userspace/usb4_rdma").iterdir():
            assert path.read_bytes() == (patched / "providers/usb4_rdma" / path.name).read_bytes()
        assert "add_subdirectory(providers/usb4_rdma)" in (patched / "CMakeLists.txt").read_text()
        assert "verbs_provider_usb4_rdma;" in (patched / "libibverbs/verbs.h").read_text()
        subprocess.run(["bash", str(repo / "packaging/test-rdma-patches.sh")],
                       env={**os.environ, "RDMA_SRC": str(source), "RDMA_CORE_TAG": "",
                            "DOCKER": str(container), "TEST_UPSTREAM": str(upstream), "TEST_REPO": str(repo)},
                       check=True, stdout=subprocess.DEVNULL)
    before = {path.name: path.read_bytes() for path in patches.iterdir()}
    # A changed upstream anchor must fail before replacing shipping patches.
    (upstream / "CMakeLists.txt").write_text("no supported build anchor\n")
    result = subprocess.run(["bash", str(repo / "packaging/regen-rdma-core-patches.sh")],
                            env={**os.environ, "RDMA_SRC": str(upstream)},
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert result.returncode and before == {path.name: path.read_bytes() for path in patches.iterdir()}
print("Provider patches: PASS (directory/archive regeneration, container staging, complete series, safe failure)")
