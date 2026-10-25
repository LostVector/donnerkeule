#!/usr/bin/env bash
# gufo-nomtp.sh NAME [reps]: both Gufo ranks by hand, q4, no speculative decoding, then the serving bench.
cd "$(dirname "$0")"
n=$1 reps=${2:-5}
tok=$(openssl rand -hex 24)
M=${GUFO_M:-/opt/models/qwen3.8-flash-next/UD-Q4_K_XL/Qwen3.8-Flash-Next-UD-Q4_K_XL-00001-of-00004.gguf}
POD='podman run --rm --name dkgufo --security-opt label=disable --userns keep-id:uid=1000,gid=1000 --device /dev/kfd --device /dev/dri --device /dev/infiniband -v /home/sn/tbv/c50:/home/sn/tbv/c50:ro -e RDMAV_DRIVERS=/home/sn/tbv/c50/libusb4_rdma --group-add keep-groups --ulimit memlock=-1 --network host -v /home/sn/gufo-merge-20261023:/workspace/gufo:ro -v /opt/models:/opt/models:ro -w /workspace/gufo localhost/gufo-tp2-dev:7.2.3 build/gpu-tp2/gufo serve llm --model '$M' --sessions 4 --max-pending 8 --tp-world-size 2 --tp-bootstrap-port 18515 --tp-control-port 18516 --tp-rdma-device usb4_rdma0 --tp-control-token '$tok
ssh fuzzy "$POD --served-model-name qwen3.8-flash-next --tp-rank 0 --host 127.0.0.1 --port 8000" > runs/gufo-$n.rank0.log 2>&1 &
sleep 5
ssh misty "$POD --tp-rank 1 --tp-bootstrap-host 192.168.211.2" > runs/gufo-$n.rank1.log 2>&1 &
for i in $(seq 1 120); do ssh fuzzy 'curl -sf -m 5 http://127.0.0.1:8000/v1/models >/dev/null' && break; sleep 10; done
ssh fuzzy "cd ~/git/gufo && timeout 2400 python3 tools/serving/gufo-serving-bench.py --fingerprint ~/dk-lab/fp.json --base-url http://127.0.0.1:8000 --model qwen3.8-flash-next --max-tokens 256 --warmup 1 --repetitions $reps --temperature 0 --note '$n nomtp' --output ~/dk-lab/gufo-$n.json" > runs/gufo-$n.txt 2>&1
tail -4 runs/gufo-$n.txt | head -3
ssh fuzzy 'podman stop -t 20 dkgufo' >/dev/null 2>&1; ssh misty 'podman stop -t 20 dkgufo' >/dev/null 2>&1; wait
