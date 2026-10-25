#!/usr/bin/env bash
# big.sh native|dmabuf: reload for mode, run READ/SEND tests uncapped into sweep-<mode>-big.csv
set -uo pipefail
mode=$1
if [[ $mode = native ]]; then extra="dmabuf_verbs=0"; label="usb4_rdma native, 2 cables"
else extra=""; label="usb4_rdma DMA-BUF verbs, 2 cables"; fi
for h in fuzzy misty; do ssh $h 'sudo rmmod thunderbolt_ibverbs' ; done
for h in fuzzy misty; do ssh $h "sudo bash ~/dk-lab/load.sh base $extra" & done; wait
sleep 20
for h in fuzzy misty; do ssh $h 'cat /sys/module/thunderbolt_ibverbs/parameters/dmabuf_verbs; ibv_devices | grep -c usb4'; done
for t in ib_read_bw ib_send_bw ib_read_lat ib_send_lat; do
  echo "### $t $(date +%T)"
  ssh fuzzy "cd ~/dk-lab && python3 sweep_perftest.py --server misty --dev usb4_rdma0 --gid 1 --label '$label' --csv sweep-$mode-big.csv --only $t" 2>&1
done
