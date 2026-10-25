# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual lane allocator with a leased outer transfer cache."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from production import source_file, functions


class CreditCacheTests(unittest.TestCase):
    def test_nested_credit_staging_does_not_reuse_or_release_outer_lease(self):
        here=Path(__file__).resolve().parent
        source = new = source_file('kernel/verbs_dmabuf.c')
        harness=(here/'repair-boundary-test.c').read_text()
        prefix=harness[:harness.index('static unsigned long jiffies;')]
        helper=new[new.index('static int tbv_dmabuf_lane_alloc('):new.index('static int tbv_dmabuf_batch_alloc_impl(')]
        tests=r'''
static bool dmabuf_reuse_buffers=true;
struct tbv_dmabuf_buffer { void *mr,*host_mr;bool uncached; };
#include "lane-allocator.c"
static void drop_private(struct tbv_dmabuf_lane *lane) {
    assert(!lane->cache);
    if(lane->staging)dma_free_coherent(lane->dev,lane->staging_bytes,lane->staging,lane->staging_dma);
    kfree(lane->frames);lane->frames=NULL;lane->staging=NULL;
}
int main(void) {
    struct device dev={0};struct tbv_rail rail={0};
    struct tbv_dmabuf_lane outer={.rail=&rail,.dev=&dev,.count=512};
    struct tbv_dmabuf_buffer gpu={.mr=(void*)1},filler={.uncached=true};
    assert(!tbv_dmabuf_lane_alloc(&outer,&gpu,true));
    assert(outer.cache && outer.cache->leased && !outer.staging);
    outer.frames[0].metadata[0]=123;
    for(int n=0;n<10;n++) {
        struct tbv_dmabuf_lane inner={.rail=&rail,.dev=&dev,.count=64};
        assert(!tbv_dmabuf_lane_alloc(&inner,&filler,true));
        assert(!inner.cache && inner.frames!=outer.frames && inner.staging);
        assert(outer.cache->leased && outer.frames[0].metadata[0]==123);
        drop_private(&inner);
        assert(outer.cache->leased && outer.frames[0].metadata[0]==123);
    }
    struct tbv_dmabuf_lane other={.rail=&rail,.dev=&dev,.count=64};
    assert(tbv_dmabuf_lane_alloc(&other,&gpu,true)==-EBUSY);
    for(int offset=1;offset<=2;offset++) {
        struct tbv_dmabuf_lane inner={.rail=&rail,.dev=&dev,.count=64};
        fail_at=attempts+offset;
        assert(tbv_dmabuf_lane_alloc(&inner,&filler,true)==-ENOMEM);
        drop_private(&inner);fail_at=0;
        assert(outer.cache->leased && outer.frames[0].metadata[0]==123);
    }
    tbv_dmabuf_cache_put(&outer,false,true);
    assert(!rail.dmabuf_cache[1]->leased);
    tbv_dmabuf_cache_purge(&rail);
    assert(!allocations && !dev.refs && !dmabuf_cache_bytes);
    puts("PASS nested credit-release allocation, lease isolation, and allocation failure cleanup");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            first=source.index('struct tbv_dmabuf_cache {');last=source.index('\n};',first)+3
            (root/'cache-struct.c').write_text(source[first:last])
            first=source.index('static void tbv_dmabuf_cache_free(');last=source.index('static void tbv_dmabuf_stop_rail(',first)
            (root/'cache-helpers.c').write_text(source[first:last])
            (root/'lane-allocator.c').write_text(helper)
            (root/'test.c').write_text(prefix+tests)
            result=subprocess.run([shutil.which('cc'),'-std=gnu11','-Wall','-Wextra','-Werror',
                                   '-I',str(root),str(root/'test.c'),'-o',str(root/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(root/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)


if __name__=='__main__':unittest.main()
