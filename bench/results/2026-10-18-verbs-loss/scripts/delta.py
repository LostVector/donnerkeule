#!/usr/bin/env python3
import re, sys
name = sys.argv[1]; runs = sys.argv[2]
keys = re.compile(r"credit_|control_(tx|rx|bad|unmatched|done_rx|notify_failed|done)\b|data_rx_lost|data_tx_(failed|error)|gpu_stopped|mismatch|abort")
def load(p):
    d = {}
    try: txt = open(p).read()
    except OSError: return d
    for i, l in enumerate(txt.splitlines()):
        m = re.match(r"\s*([a-z_0-9]+):\s*(-?\d+)\s*$", l)
        if m: d[m.group(1)] = int(m.group(2))
    return d
def peers(p):
    d = {}
    try: txt = open(p).read()
    except OSError: return d
    for m in re.finditer(r"^\s*(control_[a-z_]+|push_done_failure): (\d+)", txt, re.M):
        d[m.group(1)] = d.get(m.group(1), 0) + int(m.group(2))
    d["gpu_stopped_rails"] = len(re.findall(r"gpu_stopped=1", txt))
    return d
for h in ("fuzzy", "misty"):
    out = []
    for f, ld in (("summary", load), ("peers", peers)):
        b = ld(f"{runs}/{h}-{name}.{'before' if f=='summary' else 'peers-before'}")
        a = ld(f"{runs}/{h}-{name}.{'after' if f=='summary' else 'peers-after'}")
        for k in sorted(a):
            if keys.search(k) and a[k] != b.get(k, 0) and a[k] - b.get(k,0):
                out.append(f"{k}+{a[k]-b.get(k,0)}" if k != "gpu_stopped_rails" else f"gpu_stopped_rails={a[k]}")
        if k == "gpu_stopped_rails" or True:
            if "gpu_stopped_rails" in a and a["gpu_stopped_rails"] == 0: pass
    print(f"{h}: " + " ".join(out))
