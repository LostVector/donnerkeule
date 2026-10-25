/* SPDX-License-Identifier: GPL-2.0-only */
/* Executes the actual patch's cache and close-drain helpers against mocks. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GFP_KERNEL 0
#define SZ_2M (2u * 1024 * 1024)
#define TBV_DMABUF_BATCH_FRAMES 512
#define TBV_DMABUF_FRAME_BYTES 4096
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
typedef unsigned long long dma_addr_t;
typedef unsigned long long u64;
typedef long long atomic64_t;
static void atomic64_add(long long n, atomic64_t *v) { *v += n; }
static void atomic64_sub(long long n, atomic64_t *v) { *v -= n; }
static void atomic64_inc(atomic64_t *v) { ++*v; }
static atomic64_t dmabuf_cache_hits, dmabuf_cache_misses, dmabuf_cache_bytes;
static int allocations, attempts, fail_at;
static void *allocate(size_t n, size_t size) {
    if (++attempts == fail_at) return NULL;
    void *p=calloc(n,size); if(p) ++allocations; return p;
}
static void release(void *p) { if(p) { --allocations;free(p); } }
#define kcalloc(n,s,f) allocate(n,s)
#define kzalloc(n,f) allocate(1,n)
#define kfree(p) release(p)
struct device { int refs; };
static struct device *get_device(struct device *d) { ++d->refs;return d; }
static void put_device(struct device *d) { --d->refs; }
static void *dma_alloc_coherent(struct device *d,size_t n,dma_addr_t *address,int f) {
    (void)d;(void)f;void *p=allocate(1,n);*address=(uintptr_t)p;return p;
}
static void dma_free_coherent(struct device *d,size_t n,void *p,dma_addr_t address) {
    (void)d;(void)n;assert(address==(uintptr_t)p);release(p);
}
struct tbv_dmabuf_frame { uintptr_t metadata[16]; };
#include "cache-struct.c"
struct tbv_rail { struct tbv_dmabuf_cache *dmabuf_cache[2]; };
struct tbv_dmabuf_lane {
    struct tbv_rail *rail;struct device *dev;struct tbv_dmabuf_cache *cache;
    struct tbv_dmabuf_frame *frames;void *staging;dma_addr_t staging_dma;
    unsigned count,staging_bytes;
};
static struct { int cache_lock; } dmabuf_engine;
static void mutex_lock(int *m) { assert(!*m);*m=1; }
static void mutex_unlock(int *m) { assert(*m);*m=0; }
#include "cache-helpers.c"

static unsigned long jiffies;
static bool enabled=true;
static bool tbv_dmabuf_verbs_enabled(void) { return enabled; }
#define msecs_to_jiffies(x) (x)
#define time_before(a,b) ((long)((a)-(b)) < 0)
#define time_after_eq(a,b) (!time_before(a,b))
#define IB_QPS_ERR 6
struct tbv_qp {
    struct { unsigned qp_num; } base;
    bool dmabuf_draining,closing;unsigned sendq_count;int state,post_lock,refs;
};
struct tbv_perf_id { unsigned src_qp; };
static int refcount_read(int *r) { return *r; }
static u64 tbv_perf_clock(void) { return jiffies; }
static int recorded_status;
static void tbv_perf_span(struct tbv_perf_id *id,const char *name,u64 start,int ret,unsigned aux) {
    (void)id;(void)name;(void)start;(void)aux;recorded_status=ret;
}
static struct tbv_qp *completing;
static unsigned long finish_at;
static void msleep(unsigned n) {
    jiffies+=n;
    if(completing && time_after_eq(jiffies,finish_at)) {
        completing->sendq_count=0;completing->refs=1;completing=NULL;
    }
}
#include "drain-helper.c"

int main(void) {
    struct device device={0};struct tbv_rail rail={0};
    struct tbv_dmabuf_lane a={.rail=&rail,.dev=&device,.count=512},b=a;
    assert(!tbv_dmabuf_cache_get(&a,true,true));
    assert(dmabuf_cache_bytes==SZ_2M && device.refs==1);
    void *frames=a.frames,*staging=a.staging;
    a.frames[0].metadata[0]=123;
    assert(tbv_dmabuf_cache_get(&b,true,true)==-EBUSY);
    tbv_dmabuf_cache_put(&a,false,true);
    assert(!tbv_dmabuf_cache_get(&b,true,true));
    assert(b.frames==frames && b.staging==staging && !b.frames[0].metadata[0]);
    tbv_dmabuf_cache_purge(&rail);
    assert(device.refs==1 && rail.dmabuf_cache[1]->invalid);
    tbv_dmabuf_cache_put(&b,false,true);
    assert(!rail.dmabuf_cache[1] && !device.refs && !allocations);

    a=(struct tbv_dmabuf_lane){.rail=&rail,.dev=&device,.count=1};
    assert(!tbv_dmabuf_cache_get(&a,false,false));
    assert(!a.staging && !dmabuf_cache_bytes);
    tbv_dmabuf_cache_put(&a,false,false);
    assert(!tbv_dmabuf_cache_get(&a,false,true));
    assert(a.staging && a.staging_bytes==4096);
    tbv_dmabuf_cache_put(&a,true,false);
    assert(!allocations && !device.refs && !dmabuf_cache_bytes);

    fail_at=attempts+2;
    assert(tbv_dmabuf_cache_get(&a,true,true)==-ENOMEM);
    assert(!allocations && !device.refs && !rail.dmabuf_cache[1]);
    fail_at=attempts+3;
    assert(tbv_dmabuf_cache_get(&a,true,true)==-ENOMEM);
    tbv_dmabuf_cache_purge(&rail);
    assert(!allocations && !device.refs);
    fail_at=0;
    struct tbv_rail rails[4]={0};
    for(int i=0;i<4;i++)for(int tx=0;tx<2;tx++) {
        a=(struct tbv_dmabuf_lane){.rail=&rails[i],.dev=&device,.count=512};
        assert(!tbv_dmabuf_cache_get(&a,tx,true));tbv_dmabuf_cache_put(&a,false,tx);
    }
    assert(dmabuf_cache_bytes==16*1024*1024 && device.refs==8);
    for(int i=0;i<4;i++)tbv_dmabuf_cache_purge(&rails[i]);
    assert(!allocations && !device.refs && !dmabuf_cache_bytes);

    struct tbv_qp qp={.refs=1};jiffies=10;
    tbv_dmabuf_drain_qp(&qp);
    assert(qp.dmabuf_draining && !recorded_status && jiffies==110);
    qp=(struct tbv_qp){.refs=3,.sendq_count=2};jiffies=10;
    completing=&qp;finish_at=60;tbv_dmabuf_drain_qp(&qp);
    assert(!qp.sendq_count && qp.refs==1 && !recorded_status && jiffies==160);
    qp=(struct tbv_qp){.refs=2,.sendq_count=1};jiffies=10;
    tbv_dmabuf_drain_qp(&qp);
    assert(recorded_status==-ETIMEDOUT && jiffies==2010 && qp.sendq_count==1);
    enabled=false;qp=(struct tbv_qp){.refs=1};jiffies=10;
    tbv_dmabuf_drain_qp(&qp);assert(!qp.dmabuf_draining && jiffies==10);
    puts("PASS actual cache ownership, bounds, failure cleanup, and bounded close draining");
}
