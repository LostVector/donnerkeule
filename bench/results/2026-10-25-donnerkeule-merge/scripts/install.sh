#!/usr/bin/env bash
# install.sh: replace the DKMS thunderbolt-ibverbs 0.4.0 with 0.4.0.dk1 from ~/dk-lab/src
# and add the board tuning options. Rollback: rollback.sh. Run with sudo.
set -euo pipefail
v=0.4.0.dk1
conf=$(grep -l '^options thunderbolt_ibverbs' /etc/modprobe.d/*.conf)
bk=/home/sn/dk-lab/rollback; mkdir -p $bk
[[ -f $bk/$(basename $conf) ]] || cp -a "$conf" $bk/
# Before the DKMS install: its post-transaction dracut copies modprobe.d into the initramfs.
opts="dmabuf_batch_doorbell=1 dmabuf_post_burst=128 dmabuf_coalesce_copy=1 dmabuf_reuse_buffers=1 dmabuf_host_direct=1 dmabuf_host_credits=1 dmabuf_host_direct_min_bytes=4096"
grep -q dmabuf_batch_doorbell "$conf" || sed -i "/^options thunderbolt_ibverbs /s/\$/ $opts/" "$conf"
rsync -a --delete --exclude .git --exclude bench /home/sn/dk-lab/src/ /usr/src/thunderbolt-ibverbs-$v/
sed -i "s/^PACKAGE_VERSION=.*/PACKAGE_VERSION=\"$v\"/" /usr/src/thunderbolt-ibverbs-$v/dkms.conf
dkms status thunderbolt-ibverbs/0.4.0 | grep -q . && dkms remove thunderbolt-ibverbs/0.4.0 --all
dkms status thunderbolt-ibverbs/$v | grep -q . || dkms add thunderbolt-ibverbs/$v
for k in /usr/src/kernels/*; do dkms install thunderbolt-ibverbs/$v -k "$(basename $k)"; done
grep '^options thunderbolt_ibverbs' "$conf"
dkms status thunderbolt-ibverbs
modinfo -n thunderbolt_ibverbs; modinfo -F signer thunderbolt_ibverbs
