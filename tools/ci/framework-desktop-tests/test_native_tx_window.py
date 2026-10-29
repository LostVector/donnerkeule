# SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from production import source_file, functions


class NativeTxWindowTests(unittest.TestCase):
    def test_native_override_clamping_and_non_native_fallback(self):
        source = source_file('kernel/path.c')
        constants = '\n'.join(re.findall(r'^#define TBV_(?:NATIVE_)?DATA_TX_MAX_INFLIGHT \d+$', source, re.M))
        initializer = re.search(r'^static uint native_tx_max_inflight = [^;]+;', source, re.M).group(0)
        program = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
typedef uint32_t u32;
#define TBV_BACKEND_NATIVE 1
#define READ_ONCE(x) (x)
#define clamp_t(type,x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
struct peer { int backend; };
struct rail { struct peer *peer; };
struct tbv_path { struct rail *rail; struct { u32 tx_ring_size; } cfg; };
''' + constants + '\n' + initializer + '\n' + functions(source, ['tbv_path_tx_inflight_limit']) + r'''
int main(void) {
 struct peer peer = { TBV_BACKEND_NATIVE };
 struct rail rail = { &peer };
 struct tbv_path path = { &rail, { 1024 } };
 assert(tbv_path_tx_inflight_limit(&path) == 128);
 peer.backend = 2;
 assert(tbv_path_tx_inflight_limit(&path) == 32);
 rail.peer = NULL;
 assert(tbv_path_tx_inflight_limit(&path) == 32);
 path.rail = NULL;
 assert(tbv_path_tx_inflight_limit(&path) == 32);
 path.rail = &rail; rail.peer = &peer; peer.backend = TBV_BACKEND_NATIVE;
 native_tx_max_inflight = 32;
 assert(tbv_path_tx_inflight_limit(&path) == 32);
 native_tx_max_inflight = 64;
 assert(tbv_path_tx_inflight_limit(&path) == 64);
 native_tx_max_inflight = 128; path.cfg.tx_ring_size = 64;
 assert(tbv_path_tx_inflight_limit(&path) == 64);
 native_tx_max_inflight = 0;
 assert(tbv_path_tx_inflight_limit(&path) == 0);
 native_tx_max_inflight = UINT32_MAX; path.cfg.tx_ring_size = 1024;
 assert(tbv_path_tx_inflight_limit(&path) == 1024);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'test.c').write_text(program)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
