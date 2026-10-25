#!/usr/bin/env bash
# load.sh base|test [extra module params...]
# base: the installed DKMS module with /etc/modprobe.d options.
# test: ~/dk-lab/test-signed.ko with the same modprobe.d options plus extras.
set -euo pipefail
m=thunderbolt_ibverbs
if fuser /dev/infiniband/uverbs* >/dev/null 2>&1; then echo "verbs device busy" >&2; exit 1; fi
which=$1; shift
[[ -d /sys/module/$m ]] && rmmod $m
if [[ $which = base ]]; then
  modprobe $m "$@"
else
  opts=$(sed -n "s/^options $m //p" /etc/modprobe.d/*.conf)
  modprobe -a $(modinfo -F depends /home/sn/dk-lab/test-signed.ko | tr , ' ')
  insmod /home/sn/dk-lab/test-signed.ko $opts "$@"
fi
cat /sys/module/$m/srcversion 2>/dev/null || modinfo -F srcversion $m
