#!/usr/bin/env bash
# Regenerate the rdma-core patches that ship our libibverbs provider.
#
# Pulls a clean rdma-core source tarball (the version nixpkgs is pinned
# to), checks in our provider's source files, and writes three numbered
# patches into ./packaging/rdma-core-patches/ that any rdma-core build
# can apply. Run this after touching userspace/usb4_rdma/ files.

set -euo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$REPO_ROOT/packaging/rdma-core-patches"

# The default source comes from the repository's locked nixpkgs input.
# RDMA_SRC may point to a local source directory or archive for offline use.
RDMA_SRC=${RDMA_SRC:-$(nix build --no-link --print-out-paths "$REPO_ROOT#packages.x86_64-linux.rdma-core-source")}
echo "Using rdma-core source: $RDMA_SRC"

WORK=$(mktemp -d -t rdma-patches-XXXXXX)
trap 'rm -rf "$WORK"' EXIT

if [ -d "$RDMA_SRC" ]; then
    cp -r "$RDMA_SRC"/. "$WORK/"
else
    tar -xf "$RDMA_SRC" -C "$WORK" --strip-components=1
fi
[ -f "$WORK/CMakeLists.txt" ] && [ -f "$WORK/libibverbs/verbs.h" ] || {
    echo "Invalid rdma-core source: $RDMA_SRC" >&2
    exit 1
}
chmod -R u+w "$WORK"
cd "$WORK"

git init -q
git config user.email "ci@thunderbolt-ibverbs"
git config user.name "thunderbolt-ibverbs"
git config commit.gpgsign false
git add -A
git commit -qm "rdma-core baseline"

# Drop our provider source in.
mkdir -p providers/usb4_rdma
cp -r "$REPO_ROOT/userspace/usb4_rdma/." providers/usb4_rdma/
git add providers/usb4_rdma
git commit -qm "providers/usb4_rdma: add USB4 soft-RDMA provider

Out-of-tree provider for the usb4_rdma kernel module which exposes
Thunderbolt/USB4 host-to-host xdomain links as InfiniBand verbs
devices.

Source: https://github.com/hellas-ai/thunderbolt-ibverbs"

# Wire the provider into the build.
python3 - <<'PY'
from pathlib import Path
path = Path("CMakeLists.txt")
anchor = "add_subdirectory(providers/siw)"
text = path.read_text()
assert text.count(anchor) == 1, "Expected one siw build anchor"
path.write_text(text.replace(anchor, anchor + "\nadd_subdirectory(providers/usb4_rdma)"))
PY
git add CMakeLists.txt
git commit -qm "CMakeLists.txt: build the usb4_rdma provider"

# Declare the provider in the public header so the static-link
# all_providers.c indirection sees it. rdma-core hand-maintains this
# list — every in-tree provider has an extern in libibverbs/verbs.h.
python3 - <<'PY'
from pathlib import Path
path = Path("libibverbs/verbs.h")
anchor = "extern const struct verbs_device_ops verbs_provider_siw;"
text = path.read_text()
assert text.count(anchor) == 1, "Expected one siw provider declaration"
path.write_text(text.replace(anchor, anchor + "\nextern const struct verbs_device_ops verbs_provider_usb4_rdma;"))
PY
git add libibverbs/verbs.h
git commit -qm "libibverbs/verbs.h: declare verbs_provider_usb4_rdma

Required for the static-archive build path (libibverbs/all_providers.c)
to compile when ENABLE_STATIC=1 is on, which is the default on Debian
and most distros."

mkdir -p "$WORK/generated"
git format-patch -o "$WORK/generated" HEAD~3
python3 - "$OUT" "$WORK/generated" <<'PY'
from pathlib import Path
import shutil
import sys
out, generated = map(Path, sys.argv[1:])
series = (out / "series").read_text().splitlines()
assert len(series) == len(set(series)) == 3
assert set(series) == {path.name for path in generated.glob("*.patch")}, "Generated patch names differ from series"
for name in series:
    shutil.copyfile(generated / name, out / name)
PY

echo ""
echo "Regenerated patches in $OUT:"
ls -la "$OUT"/*.patch
