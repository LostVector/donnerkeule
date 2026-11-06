#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Fault matrices and mutation controls for real RX/TX production callbacks."""
import gzip
import json
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
FUNCTIONS = {
    'rx': {'kernel/path.c': ['tbv_frame_len', 'tbv_path_data_credit_window',
                            'tbv_native_data_consumes_rx_credit',
                            'tbv_native_data_valid_path_credit',
                            'tbv_path_rx_note_seq', 'tbv_path_rx_complete'],
           'kernel/ibdev.c': ['tbv_ibdev_rx_frame']},
    'send': {'kernel/ibdev.c': ['tbv_send_tx_done']},
    'pool': {'kernel/path.c': ['tbv_path_alloc_pooled_data_packet',
                             'tbv_path_tx_packet_release', 'tbv_path_tx_complete']},
}
MUTATIONS = [
    ('crc-gate', 'rx', 'if (frame->flags & (RING_DESC_CRC_ERROR | RING_DESC_BUFFER_OVERRUN))', 'if (false)'),
    ('raw-poison', 'rx', 'path->rx_raw_poisoned = true;', 'path->rx_raw_poisoned = false;'),
    ('credit-restart', 'rx', 'if (lost > tbv_path_data_credit_window(path->cfg.rx_ring_size))', 'if (false)'),
    ('repost-error', 'rx', 'int ret = tbv_path_post_rx_frame(f);\n\n\t\tif (ret) {',
     'int ret = tbv_path_post_rx_frame(f);\n\n\t\tif (ret && false) {'),
    ('completed-cancel', 'send', 'if (!status || tbv_send_is_completed(send))', 'if (!status)'),
    ('acked-qp-poison', 'send', 'if (failed && tbv_native_sq_ordered(tqp))', 'if ((failed || true) && tbv_native_sq_ordered(tqp))'),
    ('lost-pool-callback', 'pool', 'packet->done(packet->done_ctx, status);',
     'if (false) packet->done(packet->done_ctx, status);'),
    ('stale-frame-owner', 'pool', 'f->packet = NULL;', '(void)f->packet;'),
    ('early-pool-reuse', 'pool', 'if (packet->done)\n\t\tpacket->done',
     'if (packet->pooled && !packet->control && !packet->zcopy) list_add_tail(&packet->node, &path->tx_data_free);\n\tif (packet->done)\n\t\tpacket->done'),
    ('lost-dma-unmap', 'pool', 'if (tbv_dma_device_ready(dma_dev))',
     'if (tbv_dma_device_ready(dma_dev) && false)'),
]


def extract(path, names):
    source = path.read_text()
    bodies = []
    for name in names:
        pattern = (rf'^(?:static )?[^\n]*\b{name}\([^;{{]*?^\{{[\s\S]*?^\}}'
                   if name != 'tbv_native_data_valid_path_credit' else
                   rf'^static bool\n{name}\([^;{{]*?^\{{[\s\S]*?^\}}')
        matches = list(re.finditer(pattern, source, re.M))
        assert len(matches) == 1, (path, name, len(matches))
        match = matches[0]
        line = source[:match.start()].count('\n') + 1
        bodies.append(f'#line {line} "{path}"\n' + match[0])
    return '\n'.join(bodies)


def run(directory, name, kind, helpers, coverage=False, expected_failure=False):
    folder = directory / name
    folder.mkdir()
    template = (HERE / 'fault-tests' / (kind + '.c')).read_text()
    include = 'send-helper.c' if kind == 'send' else 'helpers.c'
    (folder / include).write_text(helpers + '\n')
    source = folder / (name + '.c')
    source.write_text(template)
    binary = folder / name
    command = [*shlex.split(os.environ.get('CC', 'cc')), '-std=gnu11', '-O0', '-g',
               '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
               '-pthread', '-I', str(ROOT)]
    if coverage:
        command.append('--coverage')
    else:
        command.append('-fsanitize=address,undefined')
    command += [str(source), '-o', str(binary)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (folder / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run([str(binary)], capture_output=True, text=True,
                            timeout=60, preexec_fn=no_core)
    (folder / 'run.log').write_text(result.stdout + result.stderr)
    if expected_failure:
        assert result.returncode != 0 and (
            'Assertion' in result.stderr or 'FAIL ' in result.stderr), (name, result)
    else:
        result.check_returncode()
        print(result.stdout, end='')
    if coverage:
        result = subprocess.run(['gcov', '--json-format', '--branch-probabilities',
                                 '--branch-counts', source.name], cwd=folder,
                                capture_output=True, text=True, check=True)
        (folder / 'gcov.log').write_text(result.stdout + result.stderr)
        return json.loads(gzip.decompress(next(folder.glob('*.gcov.json.gz')).read_bytes()))
    return None


def main(directory):
    helpers = {kind: '\n'.join(extract(ROOT / path, names)
                               for path, names in files.items())
               for kind, files in FUNCTIONS.items()}
    results = {'scope': 'Eleven extracted production functions only; not whole-driver/kernel coverage. Kernel/hardware dependencies are host stubs.',
               'functions': {}, 'mutations': []}
    for kind in FUNCTIONS:
        # Run the same inputs under sanitizers, then measure a separate build.
        # UBSan inserts failure edges that are not production source branches.
        run(directory, kind + '-sanitized', kind, helpers[kind])
        data = run(directory, kind, kind, helpers[kind], coverage=True)
        for file in data['files']:
            path = Path(file['file'])
            if path not in [ROOT / p for p in FUNCTIONS[kind]]:
                continue
            for name in FUNCTIONS[kind][str(path.relative_to(ROOT))]:
                lines = [line for line in file['lines'] if line.get('function_name') == name]
                assert lines, (name, file)
                branches = [b for line in lines for b in line.get('branches', [])]
                results['functions'][name] = dict(
                    file=str(path.relative_to(ROOT)),
                    lines=len(lines), lines_hit=sum(line['count'] > 0 for line in lines),
                    branches=len(branches), branches_hit=sum(b['count'] > 0 for b in branches))
    for name, kind, before, after in MUTATIONS:
        assert helpers[kind].count(before) == 1, name
        run(directory, 'mutant-' + name, kind, helpers[kind].replace(before, after),
            expected_failure=True)
        results['mutations'].append(dict(name=name, detected=True))
    for metric in ('lines', 'branches'):
        total = sum(r[metric] for r in results['functions'].values())
        hit = sum(r[metric + '_hit'] for r in results['functions'].values())
        results[metric] = dict(hit=hit, total=total, percent=100 * hit / total)
    (directory / 'coverage.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))
    # The denominator is the explicit function list above, never the whole file.
    # New behavior within a covered function must bring its tests with it.
    for name, result in results['functions'].items():
        for metric in ('lines', 'branches'):
            if result[metric]:
                assert result[metric + '_hit'] / result[metric] >= .95, (name, metric, result)


if __name__ == '__main__':
    output = os.environ.get('TBV_FAULT_OUTPUT')
    if output:
        destination = Path(output)
        destination.mkdir(parents=True, exist_ok=False)
        main(destination)
    else:
        with tempfile.TemporaryDirectory(prefix='tbv-fault-') as temporary:
            main(Path(temporary))
