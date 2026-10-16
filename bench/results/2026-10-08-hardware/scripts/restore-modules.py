from control import both, ssh, HOSTS, LAB, ROOT, pair, save_state
import json, shlex, time

def restore():
    both(dict.fromkeys(HOSTS, f'sudo -n {LAB}/module.sh {LAB} unload'))
    both(dict.fromkeys(HOSTS, f'sudo -n {LAB}/module.sh {LAB} restore'))
    for _ in range(30):
        state = save_state('restored')
        if all(p.stdout.count('data_ready=1') == 4 for p in state.values()):
            print('Original modules restored; four data rails ready on both hosts', flush=True)
            return
        time.sleep(1)
    raise RuntimeError('Original module peer readiness failed')

restore()
# Run the same strict error test once against the original saved binary.
# A failure remains a failure; this comparison identifies its provenance.
try:
    pair('baseline-normal-host-error-v2', ['--host-mr', '--remote-error',
         '--bytes', '10304', '--iterations', '8', '--rounds', '1', '--depth', '3',
         '--spin-us', '5000'])
except RuntimeError as e:
    print(str(e), flush=True)
finally:
    restore()

code = '''import hashlib,json,pathlib,subprocess
lab=pathlib.Path(LAB)
def check_module(name, note, params):
    root=pathlib.Path('/sys/module')/name
    expected=dict(line.split('=',1) for line in params.read_text().splitlines())
    mismatches=[key for key,value in expected.items() if (root/'parameters'/key).read_text().strip()!=value]
    return {'build_note_matches':note.read_bytes()==(root/'notes/.note.gnu.build-id').read_bytes(),
            'parameters_match':not mismatches,'mismatched_parameter_names':mismatches}
o={'verbs':check_module('thunderbolt_ibverbs',lab/'baseline.note',lab/'baseline.parameters'),
   'nhi':check_module('thunderbolt',lab/'nhi-original.note',lab/'private/nhi-original.parameters'),
   'taint':int(pathlib.Path('/proc/sys/kernel/tainted').read_text()),
   'original_taint':int((lab/'baseline.tainted').read_text()),
   'reset_methods':{dev:(pathlib.Path('/sys/bus/pci/devices')/dev/'reset_method').read_text().strip() for dev in ('0000:c6:00.5','0000:c6:00.6')},
   'baseline_ko_sha256':hashlib.sha256((lab/'baseline.ko').read_bytes()).hexdigest(),
   'signed_test_ko_sha256':hashlib.sha256((lab/'production-signed.ko').read_bytes()).hexdigest()}
(lab/'results/restored-modules.json').write_text(json.dumps(o,indent=2)+'\\n')
print(json.dumps(o))
assert all(o[k]['build_note_matches'] and o[k]['parameters_match'] for k in ('verbs','nhi'))
assert o['taint']==o['original_taint']
'''.replace('LAB', repr(LAB))
checks=both(dict.fromkeys(HOSTS, 'sudo -n python3 -c '+shlex.quote(code)))
for host,p in checks.items():
    (ROOT/(host+'-restored-modules.json')).write_text(p.stdout)
    print(host,p.stdout,flush=True)
