# SPDX-License-Identifier: GPL-2.0-only
"""Compile and execute the real repair helpers against allocation/lifecycle mocks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions


class RepairTests(unittest.TestCase):
    def test_cache_ownership_limits_failures_and_bounded_close(self):
        here = Path(__file__).resolve().parent
        source = source_file('kernel/verbs_dmabuf.c') + '\n' + source_file('kernel/ibdev.c')
        sections = [('cache-struct.c', 'struct tbv_dmabuf_cache {', '\n};', True),
                    ('cache-helpers.c', 'static void tbv_dmabuf_cache_free(', 'static void tbv_dmabuf_stop_rail(', False),
                    ('drain-helper.c', 'static void tbv_dmabuf_drain_qp(', 'static int tbv_destroy_qp(', False)]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, first, last, include_end in sections:
                start = source.index(first)
                end = source.index(last, start) + (len(last) if include_end else 0)
                (directory / name).write_text(source[start:end])
            executable = directory / 'repair-test'
            result = subprocess.run([shutil.which('cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(directory), str(here / 'repair-boundary-test.c'), '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
