#!/usr/bin/env python3
"""Record a bounded command in a private tracefs instance, then disable/remove it.

Does not change module parameters, restart services, or replace the global tracer.
Use on each Linux peer; CLOCK_MONOTONIC clocks from different hosts stay separate.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import socket
import subprocess
import time


def write(path, value):
    path.write_text(str(value))


def read_optional(path):
    try:
        return path.read_text()
    except OSError:
        return None


def read_build_id(path):
    try:
        return path.read_bytes().hex()
    except OSError:
        return None


def snapshot():
    params = Path("/sys/module/thunderbolt_ibverbs/parameters")
    build_id = Path("/sys/module/thunderbolt_ibverbs/notes/.note.gnu.build-id")
    return {
        "monotonic_ns": time.monotonic_ns(), "wall_time_ns": time.time_ns(),
        "module_parameters": {
            p.name: (read_optional(p) or "").strip() for p in sorted(params.glob("*"))
            if p.name != "dmabuf_control_bench" and os.access(p, os.R_OK)
        },
        "interrupts": read_optional(Path("/proc/interrupts")),
        "softirqs": read_optional(Path("/proc/softirqs")),
        "cpu_stat": read_optional(Path("/proc/stat")),
        "module_build_id_note": read_build_id(build_id),
        "driver_summary": read_optional(Path("/sys/kernel/debug/thunderbolt_ibverbs/summary")),
        "driver_peers": read_optional(Path("/sys/kernel/debug/thunderbolt_ibverbs/peers")),
    }


def stop_child(child):
    if child is not None and child.poll() is None:
        try:
            os.killpg(child.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()


def create_instance(path):
    path.mkdir()


def remove_instance(path):
    path.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="new capture directory")
    parser.add_argument("--tracefs", type=Path, default=Path("/sys/kernel/tracing"))
    parser.add_argument("--buffer-kb", type=int, default=2048, help="bounded buffer per CPU")
    parser.add_argument("--timeout", type=int, default=120, help="command deadline in seconds")
    parser.add_argument("--system-events", action="store_true", help="also capture scheduler/IRQ/workqueue")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or not 1 <= args.timeout <= 3600 or not 64 <= args.buffer_kb <= 65536:
        parser.error("provide a command; timeout 1..3600, buffer-kb 64..65536")
    args.out = args.out.resolve()
    args.out.mkdir(mode=0o700, parents=True, exist_ok=False)
    instance = args.tracefs / "instances" / f"tbv-perf-{os.getpid()}"
    child = None
    interrupted = False

    def interrupt(signum, frame):
        nonlocal interrupted
        if not interrupted:
            interrupted = True
            raise InterruptedError(f"received signal {signum}")

    previous_term = signal.signal(signal.SIGTERM, interrupt)
    metadata = {
        "schema": 1, "host": socket.gethostname(), "kernel": platform.release(),
        "boot_id": read_optional(Path("/proc/sys/kernel/random/boot_id")),
        "clock": "monotonic", "complete": False, "timed_out": False,
        "buffer_kb_per_cpu": args.buffer_kb, "enabled_events": [], "missing_events": [],
        "before": snapshot(),
    }
    try:
        create_instance(instance)
        write(instance / "tracing_on", 0)
        write(instance / "buffer_size_kb", args.buffer_kb)
        write(instance / "trace_clock", "mono")
        events = ["thunderbolt_ibverbs/tbv_perf"]
        if args.system_events:
            events += ["sched/sched_switch", "sched/sched_wakeup",
                       "irq/irq_handler_entry", "irq/irq_handler_exit",
                       "irq/softirq_entry", "irq/softirq_exit",
                       "workqueue/workqueue_queue_work", "workqueue/workqueue_execute_start",
                       "workqueue/workqueue_execute_end"]
        for event in events:
            enable = instance / "events" / event / "enable"
            if not enable.exists():
                metadata["missing_events"].append(event)
                if event.endswith("/tbv_perf"):
                    raise RuntimeError("tbv_perf unavailable (module unloaded, tracing disabled or TBV_PROFILING=0)")
                continue
            write(enable, 1)
            metadata["enabled_events"].append(event)
        write(instance / "trace", "")
        write(instance / "tracing_on", 1)
        with (args.out / "stdout.log").open("wb") as stdout, (args.out / "stderr.log").open("wb") as stderr:
            child = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                metadata["returncode"] = child.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                metadata["timed_out"] = True
                stop_child(child)
                metadata["returncode"] = child.returncode
        write(instance / "tracing_on", 0)
        metadata["complete"] = metadata["returncode"] == 0 and not metadata["timed_out"]
    except BaseException as error:
        metadata["error"] = f"{type(error).__name__}: {error}"
        if isinstance(error, KeyboardInterrupt):
            metadata["interrupted"] = True
    finally:
        cleanup_errors = []

        def cleanup(operation):
            try:
                operation()
            except Exception as error:
                cleanup_errors.append(f"{type(error).__name__}: {error}")

        cleanup(lambda: stop_child(child))
        if instance.exists():
            cleanup(lambda: write(instance / "tracing_on", 0))
            # Attempt every disable even if reading an artifact fails.
            for event in metadata["enabled_events"]:
                cleanup(lambda event=event: write(instance / "events" / event / "enable", 0))
            cleanup(lambda: (args.out / "kernel.trace").write_text((instance / "trace").read_text()))
            metadata["per_cpu_stats"] = {}
            for p in sorted((instance / "per_cpu").glob("cpu*/stats")):
                cleanup(lambda p=p: metadata["per_cpu_stats"].update({p.parent.name: p.read_text()}))
            cleanup(lambda: remove_instance(instance))
        if cleanup_errors:
            metadata["cleanup_errors"] = cleanup_errors
            metadata["complete"] = False
        metadata["after"] = snapshot()
        (args.out / "capture.json").write_text(json.dumps(metadata, indent=2) + "\n")
        signal.signal(signal.SIGTERM, previous_term)
    print(f"Capture saved: {args.out}; command complete={metadata['complete']}")
    return 0 if metadata["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
