#!/usr/bin/env python3
"""Hardware-independent regression scenarios for dual-node diagnostics."""
import copy
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('doctor', ROOT / 'tools/dual-strix-doctor.py')
d = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d)


def host(name, local_ids, peer_ids):
    return {'host': name, 'kernel': 'test', 'cmdline': 'ttm.pages_limit=32505856 amd_iommu=off',
        'ttm_pages_limit': '32505856', 'boot_sources': {'grub': {'readable': True}},
        'modules': {'thunderbolt': {'loaded': True, 'srcversion': 'core'},
                    'thunderbolt_ibverbs': {'loaded': True, 'srcversion': 'rdma',
                        'parameters': {'native_domain_mask': '4294967295'}},
                    'thunderbolt_net': {'loaded': False}},
        'routers': [dict(name=f'{i}-0', unique_id=u, remote=False) for i, u in enumerate(local_ids)] +
                   [dict(name=f'{i}-2', unique_id=u, remote=True, rx_gbps=20, tx_gbps=20) for i, u in enumerate(peer_ids)],
        'rails': [dict(name=f'usb4_rdma{i}', controller=f'nhi{i//2}', ports=[{'state': '4: ACTIVE'}]) for i in range(4)],
        'verbs': {'returncode': 0, 'stdout': 'usb4_rdma0 usb4_rdma1 usb4_rdma2 usb4_rdma3'},
        'driver_summary': {'available': True, 'values': {'configured_links': '0', 'native_same_peer_multicable': 'enabled'}},
        'kernel_log': {'available': True, 'errors': []}, 'processes': [], 'meminfo': {}, 'GPU': [{'mem_info_gtt_total': '100', 'mem_info_vram_total': '2'}],
        'cpu': {'scaling_governor': 'powersave', 'energy_performance_preference': 'balance_performance'}}

profile = {'boot_flags': {'ttm.pages_limit': '32505856', 'amd_iommu': 'off', 'amdgpu.gttsize': None, 'iommu': None}}
a, b = host('a', ['a0', 'a1'], ['b0', 'b1']), host('b', ['b0', 'b1'], ['a0', 'a1'])


def findings(x=a, y=b):
    return d.assess([x, y], profile)


def has(items, level, check):
    return any(x['level'] == level and x['check'] == check for x in items)

assert not any(x['level'] in ('FAIL', 'UNKNOWN', 'WARN') for x in findings())
assert d.link_rate('10.0 Gb/s', '2') == 20  # Never add RX+TX to report 40.
assert d.link_rate(None, '2') is None
assert not d.ERROR_RE.search('amdgpu: TMZ feature disabled as experimental (default)')
assert d.ERROR_RE.search('amdgpu: GPU fault detected')
for message in ('native RX bad header reason=frame_len', 'native RX lost 171 frame(s)',
                'native RX reorder timeout qpn=0x903', 'native RX descriptor rejected hw_flags=0xf',
                'native RX descriptor diagnostic crc=1', 'native RX length diagnostic frame_len=3840',
                'thunderbolt_ibverbs: native retransmit qpn=0x903'):
    assert d.ERROR_RE.search(message), message
# Historical asymmetric heap configuration and conflicting duplicate arguments.
x = copy.deepcopy(b); x['cmdline'] = 'quiet splash'
assert has(findings(a, x), 'FAIL', 'boot') and has(findings(a, x), 'FAIL', 'boot parity')
x = copy.deepcopy(a); x['cmdline'] += ' amd_iommu=on'
assert has(findings(x, b), 'FAIL', 'boot')
x = copy.deepcopy(a); x['cmdline'] += ' amdgpu.gttsize=126976'
assert has(findings(x, b), 'FAIL', 'boot') and has(findings(x, b), 'WARN', 'legacy GTT')
# Four rails are not evidence of two cables, nor two independent controllers.
x = copy.deepcopy(a); x['routers'] = [r for r in x['routers'] if r['name'] != '1-2']
assert has(findings(x, b), 'FAIL', 'physical cables')
x = copy.deepcopy(a)
for r in x['rails']: r['controller'] = 'nhi0'
assert has(findings(x, b), 'FAIL', 'RDMA rails')
x = copy.deepcopy(a); x['routers'][-1]['unique_id'] = 'different-peer'
assert has(findings(x, b), 'FAIL', 'cable peer identity')
x = copy.deepcopy(a); x['modules']['thunderbolt_ibverbs']['parameters']['native_domain_mask'] = '1'
assert has(findings(x, b), 'FAIL', 'domain mask')
x = copy.deepcopy(a); x['routers'][-1]['rx_gbps'] = None
assert has(findings(x, b), 'UNKNOWN', 'negotiated link')
x = copy.deepcopy(a); x['verbs']['stdout'] = ''
assert has(findings(x, b), 'FAIL', 'verbs provider')
x = copy.deepcopy(a); x['kernel_log']['available'] = False
assert has(findings(x, b), 'UNKNOWN', 'kernel log')
x = copy.deepcopy(a); x['ttm_pages_limit'] = None; x['GPU'] = []
assert has(findings(x, b), 'UNKNOWN', 'TTM limit') and has(findings(x, b), 'UNKNOWN', 'GPU limits')
# Secrets are deliberately not collected even when adjacent to legitimate options.
options = d.safe_options(['gufo', '--tp-control-token', 'SECRET', '--model', '/models/q4.gguf',
                          '--api-key=SECRET2', '--context=65536'])
assert options == {'--model': ['/models/q4.gguf'], '--context': ['65536']}
assert 'SECRET' not in str(options)
# Reversed ranks are valid; duplicate ranks and mismatched model/quant are not.
x, y = copy.deepcopy(a), copy.deepcopy(b)
for h, rank in ((x, '1'), (y, '0')):
    h['processes'] = [{'comm': 'gufo', 'options': {'--tp-rank': [rank], '--tp-world-size': ['2'],
        '--context': ['65536'], '--model': ['q4.gguf']}, 'sha256': 'same'}]
assert has(findings(x, y), 'PASS', 'TP ranks')
y['processes'][0]['options']['--tp-rank'] = ['1']
y['processes'][0]['options']['--model'] = ['q8.gguf']
assert has(findings(x, y), 'FAIL', 'TP ranks') and has(findings(x, y), 'FAIL', 'inference parity')
print('dual-strix-doctor: regression scenarios passed (no hardware or sudo)')
