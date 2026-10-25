# SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions


class ProgressOwnerTests(unittest.TestCase):
    def test_concurrent_timer_wake_and_stop_use_actual_helpers(self):
        here=Path(__file__).resolve().parent
        source = source_file('kernel/verbs_dmabuf.c')
        active = functions(source, ['tbv_dmabuf_progress_active'])
        startstop = functions(source, ['tbv_dmabuf_progress_start', 'tbv_dmabuf_progress_arm', 'tbv_dmabuf_progress_stop'])
        callback = functions(source, ['tbv_dmabuf_progress_timer'])
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for name,text in [('timer-active.c',active),('timer-start-stop.c',startstop),('timer-callback.c',callback)]:
                (root/name).write_text(text)
            result=subprocess.run([shutil.which('cc'),'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-pthread',
                '-I',str(root),str(here/'progress-owner-test.c'),'-o',str(root/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'test')],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':unittest.main()
