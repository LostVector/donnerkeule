# SPDX-License-Identifier: GPL-2.0-only
"""Read real production helpers; hardware mocks live in the C harnesses."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]

def source_file(path):
    return (ROOT/path).read_text()

def functions(source, names):
    selected = []
    for name in names:
        pattern = rf"^(?:static )?[^\n]*\b{name}\([^;{{]*?^\{{[\s\S]*?^\}}"
        matches = re.findall(pattern, source, re.MULTILINE)
        if len(matches) != 1:
            raise RuntimeError(f'Expected one production function: {name}')
        selected.append(matches[0])
    return '\n'.join(selected)+'\n'
