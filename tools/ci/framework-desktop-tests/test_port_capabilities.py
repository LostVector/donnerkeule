# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the production port capabilities used by kernel RDMA clients."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions


class PortCapabilitiesTests(unittest.TestCase):
    def test_rc_only_mode_excludes_management_and_preserves_gid_capabilities(self):
        helper = functions(source_file('kernel/ibdev.c'), ['tbv_get_port_immutable'])
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
typedef unsigned u32;
#define RDMA_CORE_CAP_IB_MAD 0x00000001
#define RDMA_CORE_CAP_IB_CM 0x00000004
#define RDMA_CORE_CAP_AF_IB 0x00001000
#define RDMA_CORE_CAP_ETH_AH 0x00002000
#define RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP 0x00800000
#define RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP (RDMA_CORE_CAP_IB_MAD | \
    RDMA_CORE_CAP_IB_CM | RDMA_CORE_CAP_AF_IB | RDMA_CORE_CAP_ETH_AH | \
    RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP)
#define IB_MGMT_MAD_SIZE 256
struct ib_device { int unused; };
struct ib_port_attr { unsigned pkey_tbl_len, gid_tbl_len; };
struct ib_port_immutable {
    unsigned pkey_tbl_len, gid_tbl_len, core_cap_flags, max_mad_size;
};
static bool dmabuf;
static int query_result;
static bool tbv_dmabuf_verbs_enabled(void) { return dmabuf; }
static int tbv_query_port(struct ib_device *dev, unsigned port,
                          struct ib_port_attr *attr) {
    assert(dev && port == 1);
    if (query_result) return query_result;
    attr->pkey_tbl_len = 1;
    attr->gid_tbl_len = 8;
    return 0;
}
#include "port.c"
int main(void) {
    struct ib_device dev = {0};
    struct ib_port_immutable port = {0};
    unsigned addressing = RDMA_CORE_CAP_AF_IB | RDMA_CORE_CAP_ETH_AH |
                          RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP;
    dmabuf = true;
    assert(tbv_get_port_immutable(&dev, 1, &port) == 0);
    assert(port.core_cap_flags == addressing);
    assert(port.max_mad_size == 0);
    assert(port.pkey_tbl_len == 1 && port.gid_tbl_len == 8);
    dmabuf = false;
    assert(tbv_get_port_immutable(&dev, 1, &port) == 0);
    assert(port.core_cap_flags == RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP);
    assert(port.max_mad_size == IB_MGMT_MAD_SIZE);
    assert(port.pkey_tbl_len == 1 && port.gid_tbl_len == 8);
    for (int mode = 0; mode != 2; ++mode) {
        struct ib_port_immutable before;
        memset(&port, 0xa5, sizeof(port));
        before = port;
        dmabuf = mode;
        query_result = -22;
        assert(tbv_get_port_immutable(&dev, 1, &port) == -22);
        assert(memcmp(&port, &before, sizeof(port)) == 0);
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'port.c').write_text(helper)
            (root / 'test.c').write_text(harness)
            result = subprocess.run([
                shutil.which('cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(root), str(root / 'test.c'), '-o', str(root / 'test'),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'test')], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
