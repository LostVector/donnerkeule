#!/usr/bin/env bash
# gp.sh : print gpu_stopped state of both hosts
for h in fuzzy misty; do echo -n "$h stopped_rails="; ssh $h 'sudo grep -o "gpu_stopped=1" /sys/kernel/debug/thunderbolt_ibverbs/peers | wc -l'; done
