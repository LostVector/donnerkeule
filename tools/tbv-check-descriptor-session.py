#!/usr/bin/env python3
"""Check reciprocal descriptor-session IDs in two freshly collected snapshots.

Read-only snapshot consistency check; it does not prove live/atomic state,
exercise transport, load modules, or qualify a driver configuration.
"""
import argparse
import json
from pathlib import Path
import re


def parse(text):
    ids = [value.strip() for value in re.findall(r"^native_host_id:([^\n]*)$", text, re.M)]
    if len(ids) != 1 or not re.fullmatch(r"[0-9a-f]{32}", ids[0]) or ids[0] == "0" * 32:
        raise ValueError("expected one nonzero native_host_id")
    modes = [value.strip() for value in re.findall(r"^dmabuf_verbs:([^\n]*)$", text, re.M)]
    if modes != ["1"]:
        raise ValueError("snapshot must describe descriptor mode")
    peers = re.findall(r"\bremote_host=(\S*)", text)
    if not peers or any(not re.fullmatch(r"[0-9a-f]{32}", value) for value in peers):
        raise ValueError("missing or malformed negotiated remote host identity")
    return {"local_host_id": ids[0], "remote_host_ids": sorted(set(peers))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("local_snapshot", type=Path)
    parser.add_argument("peer_snapshot", type=Path)
    args = parser.parse_args()
    try:
        hosts = [parse(p.read_text()) for p in [args.local_snapshot, args.peer_snapshot]]
        if hosts[0]["local_host_id"] == hosts[1]["local_host_id"]:
            raise ValueError("expected distinct local IDs for two hosts")
        mismatches = [i for i, h in enumerate(hosts)
                      if h["remote_host_ids"] != [hosts[1 - i]["local_host_id"]]]
        valid = not mismatches
        print(json.dumps({"reciprocal_ids_match": valid, "mismatched_snapshots": mismatches,
                          "hosts": hosts, "scope": "snapshot consistency only; collect fresh before QPs and do not reload afterward"}, indent=2))
        return 0 if valid else 1
    except (OSError, ValueError) as error:
        print(json.dumps({"reciprocal_ids_match": False, "error": str(error)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
