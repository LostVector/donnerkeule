#!/usr/bin/env bash
# Tested launch profile for an already installed, compatible Gufo TP2 stack.
# Does not install/reload drivers, change clocks, or modify services.
set -euo pipefail
if [[ $# != 1 || ! $1 =~ ^[01]$ ]]; then
 echo 'Usage: GUFO_BIN=... MODEL=... TP_HEAD=... TP_TOKEN_FILE=... tools/gufo-tp2-profile.sh 0|1' >&2
 exit 2
fi
rank=$1
: "${GUFO_BIN:?Set GUFO_BIN to your compatible Gufo TP2 executable}"
: "${MODEL:?Set MODEL to the first GGUF shard; all shards must exist on each host}"
: "${TP_HEAD:?Set TP_HEAD to rank0 management Ethernet IP/address}"
: "${TP_TOKEN_FILE:?Set TP_TOKEN_FILE to the identical private token file on both hosts}"
[[ -x "$GUFO_BIN" && -f "$MODEL" && -r "$TP_TOKEN_FILE" ]] || { echo 'Executable, model or token file unavailable' >&2;exit 2; }
if [[ -n ${ROCM_LIB_DIR:-} ]];then
 [[ -d $ROCM_LIB_DIR ]] || { echo 'ROCM_LIB_DIR does not exist' >&2;exit 2; }
 export LD_LIBRARY_PATH="$ROCM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
if [[ -n ${PROVIDER_LIB_DIR:-} ]]; then
 [[ -d "$PROVIDER_LIB_DIR" ]] &&
  compgen -G "$PROVIDER_LIB_DIR/libusb4_rdma*.so*" > /dev/null || {
   echo 'Missing libusb4_rdma provider in PROVIDER_LIB_DIR' >&2;exit 2;
  }
 export RDMAV_DRIVERS="$PROVIDER_LIB_DIR/libusb4_rdma"
 export LD_LIBRARY_PATH="$PROVIDER_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
case ${TP_PROFILE:-prefill} in
 prefill) context=65536;chunk=2048 ;;
 short) context=4096;chunk=512 ;;
 *) echo 'TP_PROFILE must be prefill or short' >&2;exit 2 ;;
esac
token=$(<"$TP_TOKEN_FILE")
[[ -n "$token" && "$token" != *$'\n'* ]] || { echo 'Token must be one nonempty line' >&2;exit 2; }
# Token intentionally never printed; avoid invoking this script with bash -x.
exec "$GUFO_BIN" serve llm --model "$MODEL" \
 --served-model-name flash-next-tp-test \
 --context "$context" --sessions 2 --prefill-chunk "$chunk" \
 --cache-ram-bytes 1073741824 --speculative off --think off \
 --host 127.0.0.1 --port 28530 --temperature 0 --seed 1 \
 --tp-world-size 2 --tp-rank "$rank" \
 --tp-bootstrap-host "$TP_HEAD" --tp-bootstrap-port 28531 \
 --tp-control-port 28532 --tp-control-token "$token" \
 --tp-rdma-device "${TP_DEVICE:-usb4_rdma0}"
