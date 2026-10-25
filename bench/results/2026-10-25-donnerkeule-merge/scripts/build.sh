#!/usr/bin/env bash
# Build and sign the test module (and GPU verifier with --hip) in ~/dk-lab.
set -euo pipefail
lab=$HOME/dk-lab
cd "$lab/src"
make modules W=1 -j16 > "$lab/build.log" 2>&1 || { tail -40 "$lab/build.log"; exit 1; }
grep -E "warning:|error:" "$lab/build.log" | grep -v pahole || true
kver=$(uname -r)
cp kernel/thunderbolt_ibverbs.ko "$lab/test.ko"
sudo /usr/src/kernels/$kver/scripts/sign-file sha256 /var/lib/dkms/mok.key /var/lib/dkms/mok.pub "$lab/test.ko" "$lab/test-signed.ko"
if [[ ${1:-} = --hip ]]; then
  mkdir -p "$lab/bin"
  podman run --rm --security-opt label=disable -v "$lab:/lab" -w /lab/src \
    localhost/gufo-tp2-dev:7.2.3 bash tools/build-gpu-verbs.sh /lab/bin/tbv_gpu_verbs > "$lab/hip-build.log" 2>&1 || { tail -30 "$lab/hip-build.log"; exit 1; }
fi
modinfo -F srcversion "$lab/test-signed.ko"
sha256sum "$lab/test-signed.ko"
