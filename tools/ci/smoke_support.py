"""Extract production C functions and compile hardware-independent shims."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def declaration(source, pattern):
    matches = re.findall(pattern, source, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"Expected one production declaration: {pattern}")
    return matches[0]


def functions(source, names):
    return "\n".join(declaration(
        source, rf"^(?:static )?[^\n]*\b{name}\([^;{{]*?^\{{[\s\S]*?^\}}"
    ) for name in names)


def run_c(name, source):
    with tempfile.TemporaryDirectory(prefix=f"tbv-{name}-") as directory:
        path = Path(directory) / "smoke.c"
        binary = Path(directory) / "smoke"
        path.write_text(source)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=gnu11",
            "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-g",
            "-fsanitize=address,undefined", "-I", str(ROOT), str(path), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
