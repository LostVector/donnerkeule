#!/usr/bin/env bash
# rollback.sh: back to DKMS thunderbolt-ibverbs 0.4.0 and the saved modprobe options. Run with sudo.
set -euo pipefail
bk=/home/sn/dk-lab/rollback
dkms remove thunderbolt-ibverbs/0.4.0.dk1 --all || true
dkms status thunderbolt-ibverbs/0.4.0 | grep -q . || dkms add thunderbolt-ibverbs/0.4.0
for k in /usr/src/kernels/*; do dkms install thunderbolt-ibverbs/0.4.0 -k "$(basename $k)"; done
cp -a $bk/*.conf /etc/modprobe.d/
