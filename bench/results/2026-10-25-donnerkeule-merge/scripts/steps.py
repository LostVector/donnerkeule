import json, statistics as st, sys
for n in sys.argv[1:]:
    d = json.load(open(f"/home/sn/dk-lab/gufo-{n}.json"))
    row = [f"{n:16}"]
    for c in ("c1", "c2", "c4"):
        ss = d["results"][c]["samples"]
        steps = [s["decode_ms"] / (s["completion_tokens"] - s["draft_accepted_tokens"]) for s in ss]
        pf = st.median(s["prefill_tokens_per_second"] for s in ss)
        acc = st.mean(s["draft_acceptance"] for s in ss)
        row.append(f"{c} ms/step={st.median(steps):6.2f} prefill={pf:5.0f} acc={acc:.2f}")
    print("  ".join(row))
