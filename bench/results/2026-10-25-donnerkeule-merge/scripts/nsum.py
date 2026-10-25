import json, statistics as st, sys
for n in sys.argv[1:]:
    try: d = json.load(open(f"/home/sn/dk-lab/gufo-{n}.json"))
    except Exception as e: print(n, e); continue
    row = [f"{n:15}"]
    for c in ("c1", "c2", "c4"):
        ss = d["results"][c]["samples"]
        row.append(f"{c} dec={st.median(s['decode_tokens_per_second'] for s in ss):6.2f} itl={st.median(s['server_inter_token_ms'] for s in ss):5.2f} pf={st.median(s['prefill_tokens_per_second'] for s in ss):5.0f}")
    print("  ".join(row))
