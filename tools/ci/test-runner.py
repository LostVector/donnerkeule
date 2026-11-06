#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression checks that the suite cannot turn build failures into green CI."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

RUNNER = Path(__file__).with_name('run-smoke.py')


class RunnerTests(unittest.TestCase):
    def test_failed_compiles_are_reported_and_other_groups_still_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            compiler = directory / 'fail.py'
            compiler.write_text("print('injected compiler failure'); raise SystemExit(7)\n")
            output = directory / 'results'
            env = dict(os.environ, CC=shlex.join([sys.executable, str(compiler)]))
            reply = subprocess.run([sys.executable, str(RUNNER), '--suite', 'proto',
                                    '--suite', 'identity', '--output', str(output)],
                                   env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(reply.returncode, 1, reply.stdout + reply.stderr)
            results = json.loads((output / 'results.json').read_text())
            self.assertEqual([r['name'] for r in results], ['proto', 'identity'])
            self.assertTrue(all(r['status'] == 'failed' for r in results))
            self.assertIn('injected compiler failure', (output / 'proto.log').read_text())
            suite = ET.parse(output / 'junit.xml').getroot()
            self.assertEqual(suite.attrib['failures'], '2')
            self.assertEqual(len(suite.findall('testcase/failure')), 2)

    def test_unknown_group_is_an_error_not_an_empty_pass(self):
        reply = subprocess.run([sys.executable, str(RUNNER), '--suite', 'typo'],
                               capture_output=True, text=True, timeout=30)
        self.assertEqual(reply.returncode, 2)

    def test_failed_log_directory_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            marker = output / 'previous-failure'
            marker.write_text('retain this')
            reply = subprocess.run([sys.executable, str(RUNNER), '--suite', 'proto',
                                    '--output', str(output)], capture_output=True,
                                   text=True, timeout=30)
            self.assertNotEqual(reply.returncode, 0)
            self.assertEqual(marker.read_text(), 'retain this')


if __name__ == '__main__':
    unittest.main()
