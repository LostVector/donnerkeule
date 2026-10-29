#!/usr/bin/env python3
"""Recompute the predeclared primary analysis from complete start-level CSV."""
import csv,itertools,json,random,statistics
from pathlib import Path
root=Path(__file__).resolve().parent
rows={r['phase']:r for r in csv.DictReader((root/'starts.csv').open(newline=''))}
assert len(rows)==16
means={name:statistics.mean(float(row[f'prefill{i}_ms']) for i in [1,2,3]) for name,row in rows.items()}
assert all(abs(means[name]-float(row['mean_prefill_ms']))<1e-8 for name,row in rows.items())
diffs=[means[f'pair{i}-control']-means[f'pair{i}-fused'] for i in range(8)]
effect=statistics.mean(diffs)
extreme=sum(abs(sum(d*s for d,s in zip(diffs,signs))/8)>=abs(effect)-1e-9 for signs in itertools.product([-1,1],repeat=8))
rng=random.Random(2026102804)
boot=sorted(sum(rng.choice(diffs) for _ in diffs)/8 for _ in range(100000))
def quantile(q):
 x=(len(boot)-1)*q;lo=int(x);hi=min(lo+1,len(boot)-1);return boot[lo]+(boot[hi]-boot[lo])*(x-lo)
control=statistics.mean(means[f'pair{i}-control'] for i in range(8));fused=control-effect
result={'paired_reductions_ms':diffs,'control_mean_ms':control,'fused_mean_ms':fused,'mean_reduction_ms':effect,'latency_reduction_percent':100*effect/control,'equivalent_prefill_rate_gain_percent':100*(control/fused-1),'exact_two_sided_p':extreme/256,'assignments':256,'bootstrap95_ms':[quantile(.025),quantile(.975)],'bootstrap_samples':100000,'seed':2026102804,'significant_positive':effect>0 and extreme/256<.05}
assert result==json.loads((root/'primary.json').read_text()), 'Recorded primary analysis differs'
print(json.dumps(result,indent=2))
