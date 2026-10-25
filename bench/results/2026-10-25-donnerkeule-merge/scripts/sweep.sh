#!/usr/bin/env bash
# sweep.sh native|dmabuf: reload the module on both hosts and run the README sweep from fuzzy.
set -uo pipefail
mode=$1
if [[ $mode = native ]]; then extra="dmabuf_verbs=0"; label="usb4_rdma native, 2 cables"
else extra=""; label="usb4_rdma DMA-BUF verbs, 2 cables"; fi
for h in fuzzy misty; do ssh $h 'sudo rmmod thunderbolt_ibverbs' ; done
for h in fuzzy misty; do ssh $h "sudo bash ~/dk-lab/load.sh base $extra" & done; wait
sleep 20
for h in fuzzy misty; do ssh $h 'cat /sys/module/thunderbolt_ibverbs/parameters/dmabuf_verbs; ibv_devices | grep -c usb4'; done
ssh fuzzy "cd ~/dk-lab && python3 sweep_perftest.py --server misty --dev usb4_rdma0 --gid 1 --label '$label' --csv sweep-$mode.csv \
  --max-size ib_read_bw=524288 --max-size ib_read_lat=524288 --max-size ib_send_bw=524288 --max-size ib_send_lat=524288" 2>&1
