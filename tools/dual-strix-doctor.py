#!/usr/bin/env python3
"""Read-only dual Strix Halo preflight. No model load, admin NVMe commands or fixes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time

BOOT_KEYS = ('amdgpu.gttsize', 'ttm.pages_limit', 'amd_iommu', 'iommu',
             'amdgpu.vm_fragment_size', 'amdgpu.gpu_recovery', 'amdgpu.noretry')
PARAMS = ('native_domain_mask', 'lanes', 'native_write_striping',
          'native_fragment_striping', 'native_rx_progress', 'native_rx_recheck_ms',
          'native_rx_active_poll_us', 'native_tx_max_inflight', 'native_enabled')
# Explicit allowlists: never serialize a process's complete command line/environment.
GUFO_OPTIONS = ('--rank', '--world-size', '--tp-rank', '--tp-world-size', '--tp',
                '--model', '--mtp', '--mtp-model', '--context', '--ctx-size',
                '--sessions', '--prefill-chunk', '--ngram-cache-mb', '--think',
                '--host', '--port', '--cache-ram-bytes', '--speculative', '--temperature',
                '--seed', '--repeat-penalty', '--presence-penalty', '--frequency-penalty',
                '--tp-rdma-device')
ENV_KEYS = ('LD_LIBRARY_PATH', 'RDMAV_DRIVERS', 'HIP_VISIBLE_DEVICES',
            'ROCR_VISIBLE_DEVICES', 'ROCP_TOOL_ATTACH')
ERROR_RE = re.compile(r'GPU reset|GPU fault|ring .*timeout|amdgpu.*(?:\bfault\b|timeout)|'
                      r'nvme.*(?:timeout|reset|I/O error)|Out of memory|Killed process|'
                      r'BUG:|Oops:|general protection fault|thunderbolt.*(?:failed|error)', re.I)


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def command(args, env=None):
    begin = time.monotonic()
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=25, env=env)
        return {'returncode': p.returncode, 'stdout': p.stdout,
                'stderr': p.stderr[:1000], 'seconds': time.monotonic() - begin}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {'returncode': None, 'stdout': '', 'stderr': str(e),
                'seconds': time.monotonic() - begin}


def boot_flags(cmdline):
    result = {}
    for arg in shlex.split(cmdline):
        key, sep, value = arg.partition('=')
        if key in BOOT_KEYS:
            result.setdefault(key, []).append(value if sep else '')
    return result


def link_rate(speed, lanes):
    match = re.fullmatch(r'([\d.]+)\s+Gb/s', speed or '')
    try:
        return float(match[1]) * int(lanes) if match else None
    except (TypeError, ValueError):
        return None


def safe_options(argv):
    result = {}
    for i, arg in enumerate(argv):
        key, sep, value = arg.partition('=')
        if key in GUFO_OPTIONS:
            result.setdefault(key, []).append(value if sep else
                (argv[i + 1] if i + 1 < len(argv) and not argv[i + 1].startswith('--') else True))
    return result


def gpu_inventory():
    result = []
    for p in sorted(Path('/sys/class/drm').glob('card[0-9]*')):
        if '-' in p.name or read(p / 'device/vendor') != '0x1002':
            continue
        d = p / 'device'
        result.append({'device': str(d.resolve()), **{key: read(d / key) for key in (
            'mem_info_gtt_total', 'mem_info_gtt_used', 'mem_info_vram_total',
            'mem_info_vram_used', 'power_dpm_force_performance_level')}})
    return result


def processes(hash_binaries):
    result = []
    for p in Path('/proc').glob('[0-9]*'):
        comm = read(p / 'comm') or ''
        if not comm.startswith(('gufo', 'ib_write', 'ib_read', 'ib_send', 'rocprof', 'bpftrace', 'fio')):
            continue
        item = {'pid': int(p.name), 'comm': comm}
        if comm.startswith('gufo'):
            try:
                argv = (p / 'cmdline').read_bytes().decode(errors='replace').split('\0')
                item['options'] = safe_options(argv)
                values = (p / 'environ').read_bytes().decode(errors='replace').split('\0')
                item['runtime_env'] = {k: v for entry in values for k, sep, v in [entry.partition('=')]
                                       if sep and k in ENV_KEYS}
                exe = p / 'exe'
                item['executable'] = str(exe.resolve())
                if hash_binaries:
                    with exe.open('rb') as f:
                        item['sha256'] = hashlib.file_digest(f, 'sha256').hexdigest()
            except OSError:
                item['process_details_unavailable'] = True
        result.append(item)
    return result


def collect(profile, hash_binaries=False):
    start = time.time()
    data = {'host': socket.gethostname(), 'kernel': os.uname().release,
            'boot_id': read('/proc/sys/kernel/random/boot_id'), 'sample_unix': start,
            'cmdline': read('/proc/cmdline'), 'GPU': gpu_inventory(),
            'ttm_pages_limit': read('/sys/module/ttm/parameters/pages_limit'),
            'boot_sources': {}, 'routers': [], 'rails': [], 'modules': {},
            'processes': processes(hash_binaries)}
    for path in [Path('/etc/default/grub'), *sorted(Path('/etc/default/grub.d').glob('*.cfg'))]:
        # These are text hints, not evaluated shell or effective next-boot arguments.
        value = read(path)
        data['boot_sources'][str(path)] = {'readable': value is not None,
            'performance_option_lines': [line for line in (value or '').splitlines()
                                       if any(k in line for k in BOOT_KEYS)]}
    for path in sorted(Path('/sys/bus/thunderbolt/devices').glob('*')):
        uid = read(path / 'unique_id')
        if not uid or ':' in path.name:
            continue
        speed = {k: read(path / k) for k in ('rx_speed', 'rx_lanes', 'tx_speed', 'tx_lanes')}
        data['routers'].append({'name': path.name, 'unique_id': uid,
            'remote': not path.name.endswith('-0'), 'device_name': read(path / 'device_name'),
            **speed, 'rx_gbps': link_rate(speed['rx_speed'], speed['rx_lanes']),
            'tx_gbps': link_rate(speed['tx_speed'], speed['tx_lanes'])})
    for p in sorted(Path('/sys/class/infiniband').glob('*')):
        data['rails'].append({'name': p.name, 'controller': str((p / 'device').resolve()),
            'ports': [{'port': q.name, 'state': read(q / 'state'),
                       'physical_state': read(q / 'phys_state')} for q in sorted((p / 'ports').glob('*'))]})
    for name in ('thunderbolt', 'thunderbolt_ibverbs', 'thunderbolt_net'):
        p = Path('/sys/module') / name
        data['modules'][name] = {'loaded': p.exists(), 'srcversion': read(p / 'srcversion'),
            'refcnt': read(p / 'refcnt'), 'parameters': {k: read(p / 'parameters' / k)
                for k in PARAMS} if name == 'thunderbolt_ibverbs' else {}}
    data['cpu'] = {k: read(Path('/sys/devices/system/cpu/cpu0/cpufreq') / k)
                   for k in ('scaling_governor', 'energy_performance_preference')}
    data['meminfo'] = {line.split(':')[0]: line.split(':', 1)[1].strip()
        for line in (read('/proc/meminfo') or '').splitlines()
        if line.split(':')[0] in ('MemTotal', 'MemAvailable', 'SwapTotal', 'SwapFree')}
    data['pressure'] = {k: read('/proc/pressure/' + k) for k in ('cpu', 'memory', 'io')}
    data['nvme'] = [{'device': p.name, 'model': read(p / 'model'), 'firmware': read(p / 'firmware_rev'),
                     'pci_path': str((p / 'device').resolve())}
                    for p in sorted(Path('/sys/class/nvme').glob('nvme[0-9]*'))]
    runtime = profile.get('runtime', {})
    env = os.environ.copy()
    env.update({k: v for k, v in runtime.items() if k in ('LD_LIBRARY_PATH', 'RDMAV_DRIVERS')})
    tool = runtime.get('ibv_devices') or shutil.which('ibv_devices')
    data['verbs'] = command([tool], env) if tool else {'returncode': None, 'stdout': '', 'stderr': 'ibv_devices not found'}
    # Explicit profile opt-in only; validate the installed helper before sudo -n.
    helper = profile.get('readback_helper')
    data['driver_summary'] = {'available': False, 'reason': 'protected readback not requested'}
    if helper:
        p = Path(helper)
        try:
            st = p.stat()
            assert p.is_absolute() and st.st_uid == 0 and not st.st_mode & 0o022
            snap = command(['sudo', '-n', str(p), 'snapshot'])
            if snap['returncode'] == 0:
                summary = snap['stdout'].split('FILE /sys/kernel/debug/thunderbolt_ibverbs/summary\n', 1)[-1].split('\nFILE ', 1)[0]
                values = dict(line.split(': ', 1) for line in summary.splitlines() if ': ' in line)
                if 'profile' in values:
                    data['driver_summary'] = {'available': True, 'values': values}
                else:
                    data['driver_summary']['reason'] = 'Native debugfs summary unavailable in helper output'
            else:
                data['driver_summary']['reason'] = snap['stderr'] or 'helper snapshot failed'
        except (OSError, AssertionError):
            data['driver_summary']['reason'] = 'helper missing, not absolute, or not root-owned/non-writable'
    journal = command(['journalctl', '-k', '-b', '--since=-30min', '-n', '500', '--no-pager', '-o', 'json'])
    # Permission-limited journal output is not evidence of a clean kernel log.
    denied = 'not seeing messages' in journal['stderr'] or 'Permission denied' in journal['stderr']
    data['kernel_log'] = {'available': journal['returncode'] == 0 and not denied,
                          'scope': 'current boot, last 30 min, at most 500 entries', 'errors': []}
    if data['kernel_log']['available']:
        for line in journal['stdout'].splitlines():
            try:
                entry = json.loads(line)
                message = entry.get('MESSAGE', '')
                if isinstance(message, str) and ERROR_RE.search(message):
                    data['kernel_log']['errors'].append({'time': entry.get('__REALTIME_TIMESTAMP'), 'message': message[:600]})
            except json.JSONDecodeError:
                continue
    data['sample_seconds'] = time.time() - start
    return data


def assess(hosts, profile):
    findings = []
    def add(level, host, check, detail):
        findings.append({'level': level, 'host': host, 'check': check, 'detail': detail})
    expected = profile.get('boot_flags', {})
    for h in hosts:
        name = h['host']
        if h.get('ttm_pages_limit') is None:
            add('UNKNOWN', name, 'TTM limit', 'Effective pages_limit unavailable')
        if not h.get('GPU') or any(g.get('mem_info_gtt_total') is None or g.get('mem_info_vram_total') is None for g in h['GPU']):
            add('UNKNOWN', name, 'GPU limits', 'Effective GTT/VRAM capacity unavailable')
        flags = boot_flags(h.get('cmdline') or '')
        if not h.get('cmdline'):
            add('UNKNOWN', name, 'boot', 'Cannot read running command line')
        for key, values in flags.items():
            if len(values) != 1:
                add('FAIL', name, 'boot', f'Duplicate {key}: {values}')
        for key, value in expected.items():
            actual = flags.get(key, [])
            if actual != ([] if value is None else [str(value)]):
                add('FAIL', name, 'boot', f'{key}: running {actual}, profile expects {value!r}')
        if expected and all(flags.get(k, []) == ([] if v is None else [str(v)]) for k, v in expected.items()):
            add('PASS', name, 'boot', 'Running performance arguments match the selected profile')
        if 'amdgpu.gttsize' in flags:
            add('WARN', name, 'legacy GTT', 'Deprecated override present; inspect effective TTM/GTT limits before changing it')
        if not all(x['readable'] for x in h.get('boot_sources', {}).values()):
            add('UNKNOWN', name, 'saved boot settings', 'Some GRUB sources are protected; running arguments above remain authoritative')
        if profile.get('kernel') and h['kernel'] != profile['kernel']:
            add('FAIL', name, 'kernel', f"{h['kernel']} differs from profile {profile['kernel']}")
        for module, version in profile.get('module_srcversions', {}).items():
            actual = h['modules'].get(module, {})
            if not actual.get('loaded') or actual.get('srcversion') != version:
                add('FAIL', name, 'module', f'{module}: expected loaded srcversion {version}, found {actual.get("srcversion")}')
        native = h['modules'].get('thunderbolt_ibverbs', {})
        add('PASS' if native.get('loaded') else 'FAIL', name, 'native RDMA', 'Module loaded' if native.get('loaded') else 'Module absent')
        if h['modules'].get('thunderbolt_net', {}).get('loaded') and native.get('loaded'):
            add('WARN', name, 'transport ownership', 'Both native RDMA and thunderbolt_net loaded; inspect which services each owns')
        for key, value in profile.get('driver_parameters', {}).items():
            actual = native.get('parameters', {}).get(key)
            if actual != str(value):
                add('UNKNOWN' if actual is None else 'FAIL', name, 'driver parameter', f'{key}: {actual}, expected {value}')
        remote = [r for r in h['routers'] if r['remote']]
        wanted = profile.get('physical_links', 2)
        add('PASS' if len(remote) == wanted else 'FAIL', name, 'physical cables', f'{len(remote)} remote routers; expected {wanted}. RDMA rails are not cable count.')
        mask = native.get('parameters', {}).get('native_domain_mask')
        if mask is not None:
            try:
                blocked = [r['name'] for r in remote if not (int(mask, 0) & (1 << int(r['name'].split('-')[0]))) ]
                if blocked:
                    add('FAIL', name, 'domain mask', f'Connected domains excluded by native_domain_mask: {blocked}')
            except ValueError:
                add('UNKNOWN', name, 'domain mask', 'Cannot decode connected domains or mask')
        for r in remote:
            rates = (r['rx_gbps'], r['tx_gbps'])
            level = 'UNKNOWN' if None in rates else ('PASS' if min(rates) >= profile.get('min_link_gbps', 20) else 'FAIL')
            add(level, name, 'negotiated link', f"{r['name']}: RX {rates[0]} / TX {rates[1]} Gb/s per direction (lane rate × lanes)")
        rails = [r for r in h['rails'] if r['name'].startswith('usb4_rdma')]
        active = [r for r in rails if any((p['state'] or '').startswith('4:') for p in r['ports'])]
        controllers = set(r['controller'] for r in active)
        rail_count = profile.get('rdma_rails', 4)
        add('PASS' if len(active) == rail_count and len(controllers) == wanted else 'FAIL', name, 'RDMA rails',
            f'{len(active)} active native rails on {len(controllers)} NHI controllers; expected {rail_count} on {wanted}')
        verbs = h['verbs']
        enumerated = set(re.findall(r'\busb4_rdma\d+\b', verbs.get('stdout', '')))
        if verbs['returncode'] != 0:
            add('UNKNOWN', name, 'verbs provider', 'Userspace enumeration unavailable: ' + verbs.get('stderr', ''))
        else:
            add('PASS' if {r['name'] for r in active} <= enumerated else 'FAIL', name, 'verbs provider', f'Userspace enumerates {sorted(enumerated)}')
        summary = h['driver_summary']
        if not summary['available']:
            add('UNKNOWN', name, 'driver counters', summary['reason'])
        else:
            values = summary['values']
            add('INFO', name, 'driver counters', 'Snapshot retained; cumulative counts alone do not identify current errors. configured_links=0 is valid in native mode.')
            if wanted > 1 and values.get('native_same_peer_multicable') != 'enabled':
                add('WARN', name, 'multi-cable identity', 'Driver does not report same-peer multi-cable support enabled')
        if not h['kernel_log']['available']:
            add('UNKNOWN', name, 'kernel log', 'Kernel journal unavailable or permission-limited')
        elif h['kernel_log']['errors']:
            add('WARN', name, 'kernel log', f"{len(h['kernel_log']['errors'])} matching recent entries; inspect timestamps, not proof of an active fault")
        else:
            add('PASS', name, 'kernel log', 'No matching error entries in the bounded journal window')
        gufo = [p for p in h['processes'] if p['comm'].startswith('gufo')]
        add('INFO', name, 'inference', f'{len(gufo)} Gufo processes; ' + ('allowlisted configuration recorded' if gufo else 'model/rank/quant/MTP parity cannot be certified while stopped'))
        other = [p for p in h['processes'] if not p['comm'].startswith('gufo')]
        if other:
            add('WARN', name, 'other workload', ', '.join(f"{p['comm']} PID {p['pid']}" for p in other))
        add('INFO', name, 'memory', f"{h['meminfo']}; GPU GTT/VRAM are separate counters, not additive physical RAM use")
        available = h['meminfo'].get('MemAvailable', '').split()
        if available:
            try:
                gib = int(available[0]) / 1048576
                if gib < profile.get('min_available_gib', 4):
                    add('WARN', name, 'available memory', f'{gib:.2f} GiB available; below configured preflight threshold')
            except ValueError:
                add('UNKNOWN', name, 'available memory', 'Cannot decode MemAvailable')

    if len(hosts) == 2:
        a, b = hosts
        for source, peer in ((a, b), (b, a)):
            peer_ids = {r['unique_id'] for r in peer['routers'] if not r['remote']}
            remotes = [r for r in source['routers'] if r['remote']]
            matched = bool(remotes) and all(r['unique_id'] in peer_ids for r in remotes)
            add('PASS' if matched else 'FAIL', source['host'], 'cable peer identity', 'Remote UUIDs match requested peer host routers' if matched else 'Remote router UUIDs do not all match the requested peer')
        for key in BOOT_KEYS:
            av, bv = boot_flags(a.get('cmdline') or '').get(key, []), boot_flags(b.get('cmdline') or '').get(key, [])
            if av != bv:
                add('FAIL', 'pair', 'boot parity', f'{key}: {a["host"]} {av}; {b["host"]} {bv}')
        if a['kernel'] != b['kernel']:
            add('WARN', 'pair', 'kernel parity', f'{a["kernel"]} vs {b["kernel"]}')
        if a['ttm_pages_limit'] != b['ttm_pages_limit']:
            add('FAIL', 'pair', 'TTM parity', f'{a["ttm_pages_limit"]} vs {b["ttm_pages_limit"]}')
        for module in ('thunderbolt', 'thunderbolt_ibverbs'):
            av = a['modules'][module].get('srcversion'); bv = b['modules'][module].get('srcversion')
            if av != bv:
                add('WARN', 'pair', 'module parity', f'{module}: {av} vs {bv}')
        for h in hosts:
            add('INFO', h['host'], 'GPU limits', f"TTM pages {h['ttm_pages_limit']}; " +
                '; '.join(f"GTT {g['mem_info_gtt_total']} / VRAM {g['mem_info_vram_total']} bytes" for g in h['GPU']) +
                '. BIOS VRAM reservations can differ despite matching boot arguments; no HIP context is opened.')
        ag = [p for p in a['processes'] if p['comm'].startswith('gufo')]
        bg = [p for p in b['processes'] if p['comm'].startswith('gufo')]
        if len(ag) == len(bg) == 1:
            aa, bb = ag[0].get('options', {}), bg[0].get('options', {})
            ranks = {tuple(aa.get('--tp-rank', [])), tuple(bb.get('--tp-rank', []))}
            if ranks != {('0',), ('1',)} or aa.get('--tp-world-size') != ['2'] or bb.get('--tp-world-size') != ['2']:
                add('FAIL', 'pair', 'TP ranks', 'Expected one explicit rank 0 and one rank 1 with world size 2')
            else:
                add('PASS', 'pair', 'TP ranks', 'One explicit rank per host, ranks 0/1, world size 2')
            for key in ('--model', '--mtp-model', '--context', '--sessions', '--prefill-chunk',
                        '--cache-ram-bytes', '--speculative', '--think', '--temperature', '--seed'):
                av, bv = aa.get(key), bb.get(key)
                if key in ('--model', '--mtp-model'):
                    av = [Path(v).name for v in av] if av else av
                    bv = [Path(v).name for v in bv] if bv else bv
                if av != bv:
                    add('FAIL', 'pair', 'inference parity', f'{key}: {av} vs {bv}')
            if '--context' not in aa or '--context' not in bb:
                add('UNKNOWN', 'pair', 'context', 'Context not explicit on both ranks; defaults depend on build')
            if ag[0].get('sha256') and bg[0].get('sha256'):
                add('PASS' if ag[0]['sha256'] == bg[0]['sha256'] else 'FAIL', 'pair', 'Gufo binary', 'Executable SHA256 compared')
            else:
                add('UNKNOWN', 'pair', 'Gufo binary', 'Use --hash-binaries to compare actual executable contents')
            add('INFO', 'pair', 'model identity', 'Filename/quant hints are not content hashes. CLI chunk is not proof of backend batch geometry.')
        elif ag or bg:
            add('UNKNOWN', 'pair', 'inference parity', 'Expected exactly one Gufo process per host for automatic rank/config comparison')
        for key in ('scaling_governor', 'energy_performance_preference'):
            if a['cpu'][key] != b['cpu'][key]:
                add('WARN', 'pair', 'CPU policy', f'{key}: {a["cpu"][key]} vs {b["cpu"][key]}; governor name alone is not a measured clock')
    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--peer', help='Management SSH hostname (no cable IP required)')
    ap.add_argument('--profile', type=Path, help='Explicit expected configuration/runtime paths as JSON')
    ap.add_argument('--json', type=Path, help='Write complete evidence JSON only when requested')
    ap.add_argument('--hash-binaries', action='store_true', help='Hash running Gufo executables (never model files)')
    ap.add_argument('--probe', action='store_true', help=argparse.SUPPRESS)
    ap.add_argument('--options-json', help=argparse.SUPPRESS)
    args = ap.parse_args()
    options = json.loads(args.options_json) if args.options_json else {
        'profile': json.loads(args.profile.read_text()) if args.profile else {}, 'hash_binaries': args.hash_binaries}
    local = collect(options['profile'], options['hash_binaries'])
    if args.probe:
        print(json.dumps(local)); return 0
    hosts = [local]
    extra = []
    if args.peer:
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.:@%+\-]*', args.peer):
            ap.error('Invalid SSH peer')
        remote_command = shlex.join(['python3', '-', '--probe', '--options-json', json.dumps(options)])
        try:
            p = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', args.peer, remote_command],
                input=Path(__file__).read_text(), text=True, capture_output=True, timeout=90)
            if p.returncode:
                raise RuntimeError(p.stderr[:500] or f'SSH exit {p.returncode}')
            hosts.append(json.loads(p.stdout))
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as e:
            extra.append({'level': 'FAIL', 'host': args.peer, 'check': 'peer probe', 'detail': str(e)})
    findings = assess(hosts, options['profile']) + extra
    result = {'schema': 1, 'hosts': hosts, 'findings': findings,
              'limits': 'Read-only preflight, not a payload/throughput test or certification of sustained clocks, HIP heap, model quality or recovery.'}
    for finding in findings:
        print('{level:7} {host}: {check}: {detail}'.format(**finding))
    print(result['limits'])
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + '\n')
        print('Evidence: ' + str(args.json))
    return 1 if any(f['level'] == 'FAIL' for f in findings) else 2 if any(f['level'] in ('WARN', 'UNKNOWN') for f in findings) else 0


if __name__ == '__main__':
    sys.exit(main())
