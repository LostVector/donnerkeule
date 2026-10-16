#!/usr/bin/env python3
"""Validate retained exchange arithmetic and full model logit/token agreement."""
from pathlib import Path
import re
root=Path(__file__).resolve().parent
sizes=[4,16384,65536,266244,1048576,4194304,8388612,33554368]
for host in ['fuzzy','misty']:
 for variant, iterations in [('verbs',128),('uncached',128),('coarse-tx',128),
                             ('final-verbs-256',256),('final-direct-256',256),
                             ('fine-rx-256',256),('final-check',16)]:
  text=(root/f'{host}-{variant}.log').read_text()
  rows=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('rank=')]
  assert [int(r['bytes']) for r in rows]==sizes
  for r in rows:
   assert int(r['iterations'])==iterations and int(r['errors'])==0
   us=float(r['exchange_us']);cpu=float(r['process_cpu_us'])
   assert abs(cpu/us-float(r['cpu_cores']))<0.001
   assert abs(int(r['bytes'])/us/1000-float(r['payload_GBps']))<0.001
  if 'verbs' not in variant:
   payload=sum(sizes)*(iterations+8)
   batches=sum((n+8388607)//8388608 for n in sizes)*(iterations+8)
   assert f'payload_bytes={payload} dma_batches={batches}' in text
 mismatch=(root/f'{host}-mismatch.log').read_text()
 assert 'expected_collective_mismatch=1' in mismatch
 assert 'payload_bytes=0 dma_batches=0' in mismatch
print('Transport: 7 cohorts x 2 hosts x 8 shapes; mismatch abort; arithmetic and errors=0 verified.')
def signature(path):
 lines=path.read_text().splitlines()
 return [line.split('] ',1)[1] for line in lines if 'batched step ' in line or 'batched member ' in line]
reference=signature(root/'fuzzy-model-verbs.log')
assert reference and len(reference)==6
for host in ['fuzzy','misty']:
 for variant in ['verbs','direct']:
  assert signature(root/f'{host}-model-{variant}.log')==reference
print('Model: all four runs have identical step membership, full-logit hashes and generated tokens.')
print('Model generation stopped at EOS after 1 and 3 tokens; NOT a tg128 speed result.')

for host in ['fuzzy','misty']:
 for variant in ['overlap-2048','overlap-control-2048']:
  assert signature(root/f'{host}-{variant}.log')==reference
 assert 'prefill_capacity=4096' in (root/f'{host}-overlap-2048.log').read_text()
 assert 'prefill_capacity=2048' in (root/f'{host}-overlap-control-2048.log').read_text()
print('Overlap: aligned 2048-token runs match old direct and verbs full-logit/token signatures.')
ragged=signature(root/'fuzzy-overlap-ragged-direct.log')
assert len(ragged)==5
for host in ['fuzzy','misty']:
 for variant in ['direct','verbs']:
  path=root/f'{host}-overlap-ragged-{variant}.log'
  assert signature(path)==ragged
  assert 'prefill complete: 288 exchanges' in path.read_text()
print('Overlap: 383/385-token threshold and ragged halves match on both ranks and transports.')

for size, tokens, exchanges in [('32k',65536,3072)]:
 reference_large=signature(root/f'fuzzy-overlap-{size}-verbs.log')
 assert len(reference_large)==10
 for host in ['fuzzy','misty']:
  for variant in ['verbs','direct']:
   path=root/f'{host}-overlap-{size}-{variant}.log'
   assert signature(path)==reference_large
   assert f'prefill_tokens={tokens} ' in path.read_text()
   assert f'prefill complete: {exchanges} exchanges of [20971520B x{exchanges}]' in path.read_text()
 print(f'Overlap {size}: full-logit/token signatures, token and exchange counts match.')
