#!/usr/bin/env bash
# Build the standard libibverbs/HIP producer and complete GPU verifier.
set -euo pipefail
repo="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${1:-/tmp/tbv_gpu_verbs}"
args=(-O2 -std=c++17 --offload-arch="${GPU_ARCH:-gfx1151}" -Wall -Wextra -Werror)
if [[ ${TBV_PROFILING:-1} == 0 ]]; then
    args+=(-DTBV_DISABLE_PROFILING)
fi
if [[ -n ${HIP_PATH:-} ]]; then
    args+=(--rocm-path="$HIP_PATH")
fi
if [[ -n ${HIP_DEVICE_LIB_PATH:-} ]]; then
    args+=(--rocm-device-lib-path="$HIP_DEVICE_LIB_PATH")
fi
if [[ -n ${VERBS_INCLUDE:-} && -n ${VERBS_LIB:-} ]]; then
    args+=(-I"$VERBS_INCLUDE" -L"$VERBS_LIB" -Wl,-rpath,"$VERBS_LIB" -libverbs)
else
    read -r -a verbs_flags <<< "$(pkg-config --cflags --libs libibverbs)"
    args+=("${verbs_flags[@]}")
fi
exec "${HIPCC:-hipcc}" "${args[@]}" "$repo/userspace/bench/tbv_gpu_verbs.hip" -o "$out"
