#!/usr/bin/env python3
"""Run hardware-independent production regressions; failures always fail CI."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
C_TESTS = {
    "verbs-dmabuf": [],
    "proto": [],
    "reliability": ["proto/reliability.c"],
    "identity": ["proto/identity.c"],
    "config": ["proto/config.c", "proto/identity.c"],
}
PY_TESTS = {
    name: f"tools/ci/{name}-smoke.py" for name in (
        "source-layout", "provider-patches", "verbs-control-state",
        "verbs-control-lifetime", "verbs-dma-wait", "verbs-post",
        "verbs-rx-state", "host-credit-peek", "configfs-state", "bench-helpers",
        "profiling", "framework-desktop", "rx-error-recovery", "descriptor-session",
        "dual-strix-doctor", "fault-injection", "build-contract",
    )
}
PY_TESTS.update({
    "test-runner": "tools/ci/test-runner.py",
    "native-rc-order": "tools/ci/native-rc-order-tests/run-order.py",
    "native-tx-retirement": "tools/ci/native-rc-order-tests/run-retirement.py",
})


def commands(name, directory):
    if name in PY_TESTS:
        return [[sys.executable, str(ROOT / PY_TESTS[name])]]
    binary = str(directory / name)
    return [[
        *shlex.split(os.environ.get("CC", "cc")), "-std=c11",
        "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined",
        "-I", str(ROOT), str(ROOT / f"tools/ci/{name}-smoke.c"),
        *(str(ROOT / source) for source in C_TESTS[name]), "-o", binary,
    ], [binary]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    names = list(C_TESTS) + list(PY_TESTS)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--suite", action="append", choices=names,
                        help="Run selected groups; repeat to select more than one")
    parser.add_argument("--output", type=Path,
                        help="Create a new directory for logs, JSON and JUnit")
    args = parser.parse_args()
    if args.list:
        print("\n".join(names))
        return 0
    output = args.output
    if output is not None:
        output = output.resolve()
        output.mkdir(parents=True, exist_ok=False)
    selected = list(dict.fromkeys(args.suite or names))
    env = dict(os.environ)
    env.setdefault("TBV_TEST_SANITIZERS", "1")
    if output:
        env.setdefault("TBV_FAULT_OUTPUT", str(output / "fault-injection"))
    results = []
    with tempfile.TemporaryDirectory(prefix="tbv-smoke-") as temporary:
        for name in selected:
            start = time.monotonic()
            entry = dict(name=name, commands=[], status="passed")
            log = (output / f"{name}.log").open("w") if output else None
            try:
                print(f"RUN {name}", flush=True)
                for command in commands(name, Path(temporary)):
                    entry["commands"].append(command)
                    subprocess.run(command, cwd=ROOT, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, check=True, timeout=180)
            except (OSError, subprocess.SubprocessError) as error:
                entry.update(status="failed", error=str(error))
            finally:
                if log:
                    log.close()
            entry["seconds"] = time.monotonic() - start
            results.append(entry)
            print(f"{entry['status'].upper()} {name}", flush=True)
            if output:
                (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    failures = sum(r["status"] != "passed" for r in results)
    if output:
        suite = ET.Element("testsuite", name="driver-cpu", tests=str(len(results)),
                           failures=str(failures))
        for entry in results:
            case = ET.SubElement(suite, "testcase", name=entry["name"],
                                 time=f"{entry['seconds']:.6f}")
            if entry["status"] != "passed":
                ET.SubElement(case, "failure", message=entry["error"]).text = (
                    output / f"{entry['name']}.log").read_text()
        ET.ElementTree(suite).write(output / "junit.xml", encoding="utf-8", xml_declaration=True)
    print(f"{len(results) - failures}/{len(results)} groups passed", flush=True)
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
