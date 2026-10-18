#!/usr/bin/env bash
# Use the existing Nix HIP runtime without altering system libraries.
set -euo pipefail
if [[ ${1:-} != --inside ]]; then
    exec unshare --mount --propagation private "$0" --inside "$@"
fi
shift
lab=${1:?provide the isolated lab directory}
shift
root="$lab/runtime-root"
mkdir -p "$root"/{nix,dev,sys,proc,lab,tmp}
mount --bind /home/sn/nixroot/nix "$root/nix"
mount -o remount,bind,ro "$root/nix"
mount --rbind /dev "$root/dev"
mount --make-rslave "$root/dev"
mount --bind /sys "$root/sys"
mount -o remount,bind,ro "$root/sys"
mount -t proc proc "$root/proc"
mount --bind "$lab" "$root/lab"
export IBV_DRIVERS=/lab/libusb4_rdma
export LD_LIBRARY_PATH=/lab/verbs-runtime
exec chroot "$root" /lab/tbv_gpu_verbs "$@"
