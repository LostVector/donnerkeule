#!/usr/bin/env bash
# trace.sh on|off NAME
T=/sys/kernel/tracing
case $1 in
on) for h in fuzzy misty; do ssh $h "sudo sh -c 'echo 0 > $T/tracing_on; echo 65536 > $T/buffer_size_kb; echo > $T/trace; echo \"phase ~ \\\"credit*\\\" || phase ~ \\\"control_exchange\\\" || phase ~ \\\"control_reply\\\" || phase ~ \\\"tx_direct\\\" || phase ~ \\\"rx_finish\\\" || phase ~ \\\"status*\\\" || phase ~ \\\"prepare*\\\" || phase ~ \\\"rnr*\\\"\" > $T/events/thunderbolt_ibverbs/tbv_perf/filter; echo 1 > $T/events/thunderbolt_ibverbs/tbv_perf/enable; echo 1 > $T/tracing_on'"; done;;
off) for h in fuzzy misty; do ssh $h "sudo sh -c 'echo 0 > $T/tracing_on; cat $T/trace; echo 0 > $T/events/thunderbolt_ibverbs/tbv_perf/enable'" > runs/$h-$2.trace; done; wc -l runs/*-$2.trace;;
esac
