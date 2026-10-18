#!/bin/bash
# since.sh HOST UPTIME: kernel log lines newer than UPTIME seconds
ssh "$1" 'sudo dmesg' | awk -F'[][]' -v t="$2" '$2+0>t'
