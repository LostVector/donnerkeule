#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Plain make/DKMS must still build modules when test targets are added."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='tbv-build-contract-') as temporary:
    directory = Path(temporary)
    kernel = directory / 'kernel'
    kernel.mkdir()
    marker = directory / 'module-requested'
    (kernel / 'Makefile').write_text('modules:\n\t@echo modules > ' + str(marker) + '\n')
    overlay = directory / 'overlay.mk'
    # If the default regresses to `test`, fail immediately instead of recursively
    # starting this same test suite. Real module compilation is mocked here only.
    overlay.write_text('test:\n\t@echo WRONG_DEFAULT\n\t@exit 91\n')
    command = ['make', '--no-print-directory', '-f', str(ROOT / 'Makefile'),
               '-f', str(overlay), 'KERNEL_DIR=' + str(kernel)]
    good = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=15)
    assert good.returncode == 0 and marker.read_text().strip() == 'modules', good
    marker.unlink()
    mutant = directory / 'old-default.mk'
    source = (ROOT / 'Makefile').read_text()
    needle = '.DEFAULT_GOAL := all\n'
    assert source.count(needle) == 1
    mutant.write_text(source.replace(needle, ''))
    command[3] = str(mutant)
    bad = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=15)
    assert bad.returncode != 0 and 'WRONG_DEFAULT' in bad.stdout and not marker.exists(), bad
print('PASS: plain make requests modules; old first-target regression fails without kernel headers or recursive tests')
