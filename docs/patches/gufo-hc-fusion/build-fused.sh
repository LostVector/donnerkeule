#!/usr/bin/env bash
set -euo pipefail
source_dir=$(realpath "${1:?Usage: build-baseline.sh PATCHED_GUFO_SOURCE BUILD_DIRECTORY [TARGET]}")
build_dir=$(realpath -m "${2:?Provide a separate build directory}")
rocm_root=${ROCM_PATH:-/opt/rocm}
[[ -f "$source_dir/CMakePresets.json" && -x "$rocm_root/bin/amdclang++" ]] || { echo 'Missing patched source or ROCm compiler; set ROCM_PATH.' >&2; exit 2; }
bundle_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $(git -C "$source_dir" rev-parse HEAD) = 0b6978aa1709f2ef863f5407e4ca7261191e9046 ]] || { echo 'Wrong Gufo base commit for this bundle.' >&2; exit 2; }
(cd "$source_dir"; sha256sum --check "$bundle_dir/runtime-sha256.txt")
export HIP_PATH="$rocm_root" ROCM_PATH="$rocm_root"
export LD_LIBRARY_PATH="$rocm_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
prefix="$rocm_root"
pkgconf=
# Optional isolated development-library sysroot, matching the measured setup.
# Ordinary installations can leave GUFO_DEPS_SYSROOT unset.
if [[ -n ${GUFO_DEPS_SYSROOT:-} ]]; then
 export PKG_CONFIG_SYSROOT_DIR="$GUFO_DEPS_SYSROOT"
 export PKG_CONFIG_LIBDIR="$GUFO_DEPS_SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
 export LD_LIBRARY_PATH="$GUFO_DEPS_SYSROOT/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
 prefix="$prefix;$GUFO_DEPS_SYSROOT/usr"
 if [[ -x "$GUFO_DEPS_SYSROOT/usr/bin/pkgconf" ]]; then pkgconf="$GUFO_DEPS_SYSROOT/usr/bin/pkgconf";fi
fi
if [[ -z "$pkgconf" ]]; then pkgconf=$(command -v pkgconf || command -v pkg-config);fi
(
 cd "$source_dir"
 cmake --preset release -B "$build_dir" \
  -DGUFO_BUILD_TOOLS=ON -DGUFO_ENABLE_TP2_RDMA=ON \
  -DGUFO_TP_TRACE_DIAGNOSTICS=OFF -DCMAKE_HIP_ARCHITECTURES=gfx1151 \
  -DCMAKE_C_COMPILER="$rocm_root/bin/amdclang" \
  -DCMAKE_CXX_COMPILER="$rocm_root/bin/amdclang++" \
  -DCMAKE_HIP_COMPILER="$rocm_root/lib/llvm/bin/clang++" \
  -DPKG_CONFIG_EXECUTABLE="$pkgconf" -DCMAKE_PREFIX_PATH="$prefix"
)
cmake --build "$build_dir" --target "${3:-gufo}" --parallel "${GUFO_BUILD_JOBS:-4}"
