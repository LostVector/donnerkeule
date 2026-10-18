#!/usr/bin/env python3
import hashlib,json,os,pathlib,sys,time,urllib.request,urllib.error
os.umask(0o077)
lab=pathlib.Path('/home/sn/tbv/hardware-20261007-xdomain-done')
label=sys.argv[1];tokens=int(sys.argv[2]);assert tokens in (16,128)
words=int(sys.argv[3])
prompt=' hello'*words+'\nWrite an extremely long list of integers separated by commas, starting at 1. Keep counting until 10000. Do not add explanations.'
body={'model':'qwen3.8-flash-next','messages':[{'role':'user','content':prompt}],'max_tokens':tokens,'temperature':0,'seed':42,'cache_prompt':False,'stream':False}
req=urllib.request.Request('http://127.0.0.1:19490/v1/chat/completions',json.dumps(body).encode(),{'Content-Type':'application/json'})
start=time.monotonic()
try:
 with urllib.request.urlopen(req,timeout=300) as r:raw=r.read()
except urllib.error.HTTPError as e:
 raw=e.read();(lab/'private'/(label+'-http-error.json')).write_bytes(raw)
 sys.exit('HTTP request failed: '+str(e.code)+' '+raw.decode())
o=json.loads(raw);(lab/'private'/(label+'-response.json')).write_bytes(raw)
assert o['usage']['completion_tokens']==tokens
assert o['choices'][0]['finish_reason']=='length'
timings=o.get('timings')
if timings is None:
 g=o['usage']['gufo'];timings={'prompt_n':g['prefill_tokens'],'prompt_ms':g['prefill_ms'],'predicted_n':o['usage']['completion_tokens'],'predicted_ms':g['decode_ms'],'cache_n':g['cached_prompt_tokens']}
 timings['predicted_per_second']=1000*timings['predicted_n']/timings['predicted_ms']
summary={'label':label,'wall_seconds':time.monotonic()-start,'prompt_sha256':hashlib.sha256(prompt.encode()).hexdigest(),'text_sha256':hashlib.sha256(o['choices'][0]['message']['content'].encode()).hexdigest(),'usage':o['usage'],'timings':timings}
(lab/'results'/(label+'.json')).write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary))
