# SPDX-License-Identifier: GPL-2.0-only
#!/usr/bin/env python3
"""Exercise the actual optional posting helper with a mocked ring/MMIO device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions


class BulkPostingTests(unittest.TestCase):
    def test_descriptor_order_wrap_capacity_and_fallback(self):
        compiler = shutil.which('cc')
        self.assertIsNotNone(compiler, 'A C compiler is required for the posting boundary test')
        added = source_file('kernel/verbs_dmabuf.c')
        first = added.index('struct tbv_bulk_desc {')
        last = added.index('\nfallback:', first)
        last = added.index('\n}', last) + 2
        helper = added[first:last]
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory/'helper.c').write_text(helper)
            executable = directory/'posting-test'
            result = subprocess.run([compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-I', str(directory), str(Path(__file__).with_name('bulk-post-test.c')), '-o', str(executable)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
