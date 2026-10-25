# SPDX-License-Identifier: GPL-2.0-only
"""Execute the production CQ constructor against old/new kernel responses."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions

class ProviderCQTests(unittest.TestCase):
    def test_capacity_failure_and_allocation_cleanup(self):
        helper = functions(source_file('userspace/usb4_rdma/usb4_rdma.c'), ['usb4_rdma_create_cq'])
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
struct ibv_context {int unused;};
struct ibv_comp_channel {int unused;};
struct ibv_cq {int cqe;};
struct ib_uverbs_create_cq_resp {int unused;};
struct ibv_create_cq {int unused;};
static int response_capacity, command_error, allocations, command_calls;
static bool fail_alloc;
static void *mock_calloc(size_t n, size_t size) {
    if (fail_alloc) {errno=ENOMEM;return NULL;}
    void *p=calloc(n,size);if(p)allocations++;return p;
}
static void mock_free(void *p) {if(p)allocations--;free(p);}
static int ibv_cmd_create_cq(struct ibv_context *ctx, int n,
    struct ibv_comp_channel *channel, int vector, struct ibv_cq *cq,
    struct ibv_create_cq *cmd, size_t cmd_len,
    struct ib_uverbs_create_cq_resp *resp, size_t resp_len) {
    (void)ctx;(void)channel;(void)vector;(void)cmd;(void)resp;
    assert(n>0 && cmd_len==sizeof(*cmd) && resp_len==sizeof(*resp));
    command_calls++;cq->cqe=response_capacity;return command_error;
}
#define calloc mock_calloc
#define free mock_free
#include "cq-helper.c"
int main(void) {
    struct ibv_context ctx={0};struct ibv_cq *cq;
    for(int n=1;n<=65536;n*=2) {
        response_capacity=0;cq=usb4_rdma_create_cq(&ctx,n,NULL,0);
        assert(cq && cq->cqe==n && allocations==1);mock_free(cq);
        response_capacity=n+64;cq=usb4_rdma_create_cq(&ctx,n,NULL,0);
        assert(cq && cq->cqe==n+64);mock_free(cq);
    }
    command_error=EINVAL;errno=0;
    assert(!usb4_rdma_create_cq(&ctx,32,NULL,0) && errno==EINVAL && allocations==0);
    fail_alloc=true;errno=0;int previous=command_calls;
    assert(!usb4_rdma_create_cq(&ctx,32,NULL,0) && errno==ENOMEM);
    assert(command_calls==previous && allocations==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'cq-helper.c').write_text(helper)
            (root/'test.c').write_text(harness)
            subprocess.run([shutil.which('cc'),'-std=gnu11','-Wall','-Wextra','-Werror',
                str(root/'test.c'),'-o',str(root/'test')],check=True,capture_output=True,text=True)
            result = subprocess.run([str(root/'test')],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
