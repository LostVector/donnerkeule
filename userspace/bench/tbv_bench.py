"""Small common helpers for benchmark scripts; no work at import time."""
from __future__ import annotations

import shlex
import subprocess
from collections.abc import Iterator, Sequence


def run_local(args: list[str], *, check: bool = True, timeout: int | None = None) -> subprocess.CompletedProcess:
    proc = subprocess.run(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(proc.stdout)
    return proc


def parse_lat(stdout: str) -> dict[str, str]:
    for parts in summary_rows(stdout, 9):
        return {
            "lat_min_us": parts[2],
            "lat_max_us": parts[3],
            "lat_typical_us": parts[4],
            "lat_avg_us": parts[5],
            "lat_stdev_us": parts[6],
            "lat_p99_us": parts[7],
            "lat_p999_us": parts[8],
        }
    raise ValueError("could not parse latency summary")


def ssh_args(target: str, command: str, *, config: str | None = None,
             options: Sequence[str] = ()) -> list[str]:
    args = ["ssh"]
    if config:
        args.extend(["-F", config])
    args.extend(["-o", "ConnectTimeout=8", "-o", "BatchMode=yes"])
    for option in options:
        args.extend(["-o", option])
    args.extend([target, "sudo -n bash -lc " + shlex.quote(command)])
    return args


def summary_rows(stdout: str, columns: int) -> Iterator[list[str]]:
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) >= columns and parts[0].isdigit() and parts[1].isdigit():
            yield parts
