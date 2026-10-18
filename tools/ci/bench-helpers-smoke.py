#!/usr/bin/env python3
"""Check shared helper semantics without SSH or RDMA hardware."""
import ast
import sys
from typing import Any
from smoke_support import ROOT, run_c

sys.path.insert(0, str(ROOT / "userspace/bench"))
from tbv_bench import parse_lat, run_local, ssh_args, summary_rows


def load_functions(filename, names):
    tree = ast.parse((ROOT / "userspace/bench" / filename).read_text())
    nodes = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
    assert len(nodes) == len(names)
    namespace = {"Any": Any, "summary_rows": summary_rows, "bench_ssh_args": ssh_args,
                 "SSH_CONFIG": "/tmp/test config", "SSH_OPTIONS": ["Port=2222"]}
    # Extracting the functions avoids the scripts' lab environment setup.
    exec(compile(ast.Module(body=nodes, type_ignores=[]), filename, "exec"), namespace)
    return namespace


sweep = load_functions("tbv_rdma_sweep.py", ("parse_bw", "ssh_args"))
runner = load_functions("tbv_perftest_runner.py", ("parse_bw", "ssh_args"))
bandwidth = "#bytes #iterations BW peak BW average MsgRate\n4096 1000 8.000 7.125 0.250000\n8192 1000 9.0 8.0 0.125000\n"
assert sweep["parse_bw"](bandwidth) == {"bw_peak_gbps": "8.000", "bw_avg_gbps": "7.125", "msg_rate_mpps": "0.250000"}
assert runner["parse_bw"](bandwidth, False)["bw_avg_gbps"] == "8.00"
assert runner["parse_bw"](bandwidth, True)["bw_avg_gbps"] == "15.12"
assert runner["parse_bw"](bandwidth, True)["msg_rate_mpps"] == "0.375000"
assert runner["parse_bw"]("1 2 invalid row 3\n" + bandwidth, False)["bw_avg_gbps"] == "8.00"
latency = "header\n2 100 1.00 8.00 2.00 3.00 0.10 6.00 7.00\n"
assert parse_lat(latency)["lat_avg_us"] == "3.00"
for parser, args in ((parse_lat, ()), (sweep["parse_bw"], ()), (runner["parse_bw"], (False,))):
    try:
        parser("no numeric result", *args)
    except ValueError:
        pass
    else:
        raise AssertionError("Malformed output accepted")
command = "printf '%s' 'a b; c'"
assert sweep["ssh_args"]("host", command) == ssh_args("host", command)
assert runner["ssh_args"]("host", command) == ssh_args("host", command, config="/tmp/test config", options=["Port=2222"])
assert run_local([sys.executable, "-c", "print('ok')"]).stdout == "ok\n"
assert run_local([sys.executable, "-c", "raise SystemExit(7)"], check=False).returncode == 7
try:
    run_local([sys.executable, "-c", "print('failure'); raise SystemExit(7)"])
except RuntimeError as error:
    assert str(error) == "failure\n"
else:
    raise AssertionError("Process failure accepted")

run_c("bench-helpers", r"""
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
static ssize_t simulated_send(int fd, const void *p, size_t len, int flags);
static ssize_t simulated_recv(int fd, void *p, size_t len, int flags);
#define send simulated_send
#define recv simulated_recv
#include "userspace/bench/tbv_bench.h"
#undef send
#undef recv
static unsigned int sends, recvs;
static int eof;
static ssize_t simulated_send(int fd, const void *p, size_t len, int flags) {
    if (!sends++) { errno = EINTR; return -1; }
    return len > 2 ? 2 : (ssize_t)len;
}
static ssize_t simulated_recv(int fd, void *p, size_t len, int flags) {
    if (eof) return 0;
    if (!recvs++) { errno = EINTR; return -1; }
    return len > 2 ? 2 : (ssize_t)len;
}
int main(void) {
    char buffer[5] = {0};
    assert(!tbv_bench_send_all(0, buffer, sizeof(buffer)) && sends == 4);
    assert(!tbv_bench_recv_all(0, buffer, sizeof(buffer)) && recvs == 4);
    eof = 1; assert(tbv_bench_recv_all(0, buffer, sizeof(buffer)) == -1);
    uint64_t first = tbv_bench_now_ns(); assert(tbv_bench_now_ns() >= first);
    puts("Benchmark helpers: PASS (partial I/O, EINTR, EOF, parsers, SSH arguments, process errors)");
    return 0;
}
""")
