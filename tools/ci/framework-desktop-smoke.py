#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run short, hardware-independent Framework Desktop regression tests."""
from pathlib import Path
import unittest

if __name__ == '__main__':
    directory = Path(__file__).with_name('framework-desktop-tests')
    suite = unittest.defaultTestLoader.discover(str(directory))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
