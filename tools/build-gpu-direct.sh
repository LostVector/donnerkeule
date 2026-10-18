#!/usr/bin/env bash
# Build the standalone GPU producer/consumer against an existing HIP SDK.
set -euo pipefail
repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${1:-/tmp/tbv_gpu_direct}"
args=(-O2 -std=c++17 --offload-arch="${GPU_ARCH:-gfx1151}" -Wall -Wextra -Werror)
if [[ -n ${HIP_PATH:-} ]]; then
    args+=(--rocm-path="$HIP_PATH")
fi
if [[ -n ${HIP_DEVICE_LIB_PATH:-} ]]; then
    args+=(--rocm-device-lib-path="$HIP_DEVICE_LIB_PATH")
fi
exec "${HIPCC:-hipcc}" "${args[@]}" "$repo/userspace/bench/tbv_gpu_direct.hip" -o "$out"
