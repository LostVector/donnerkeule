#!/usr/bin/env python3
"""Check production rail posting order, tails and failures with mock rings."""
from smoke_support import ROOT, functions, run_c

source = (ROOT / 'kernel/verbs_dmabuf.c').read_text()
body = functions(source, ('tbv_dmabuf_lane_post', 'tbv_dmabuf_post_group'))
shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "tools/ci/perf-noop.h"
typedef unsigned int u32;
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min_t(t,a,b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define atomic64_inc(p) (++*(p))
struct work_struct { int unused; };
struct ring_frame { unsigned index; };
struct tb_ring { unsigned lane, next; };
struct state { unsigned data_tx_posted; };
struct peer { struct state *state; };
struct tbv_rail {
    struct { struct tb_ring *tx_ring, *rx_ring; unsigned data_tx_posted; } path;
    struct peer *peer;
    unsigned rail_id;
    bool ready;
};
struct tbv_dmabuf_frame { struct ring_frame frame; };
struct tbv_dmabuf_lane {
    struct tbv_rail *rail;
    void *dev;
    unsigned count;
    struct tbv_dmabuf_frame *frames;
    struct tbv_perf_id perf;
    u64 perf_post_started;
};
struct tbv_dmabuf_batch {
    struct tbv_dmabuf_lane lanes[4];
    struct tbv_perf_id perf;
    unsigned count;
    bool tx;
};
struct tbv_dmabuf_post_group {
    struct work_struct work;
    struct tbv_dmabuf_batch *batch;
    void *dev;
    int status;
    u64 perf_queued_ns;
};
static unsigned posts, fail_at, seen[4096];
static bool tbv_dmabuf_rail_ready(struct tbv_rail *r) { return r->ready; }
static int post(struct tb_ring *r, struct ring_frame *f) {
    if (posts == fail_at) return -EIO;
    assert(f->index == r->next++);
    assert(posts < 4096);
    seen[posts++] = r->lane;
    return 0;
}
#define tb_ring_tx post
#define tb_ring_rx post
'''
cases = r'''
static void check(bool tx, unsigned a, unsigned b, unsigned fail, bool ready) {
    struct state state = {0};
    struct peer peer = {&state};
    struct tb_ring rings[4] = {{.lane=0}, {.lane=1}, {.lane=2}, {.lane=3}};
    struct tbv_rail rails[4] = {0};
    struct tbv_dmabuf_frame frames[4][512] = {0};
    struct tbv_dmabuf_batch batch = {.count=4, .tx=tx};
    int devices[2];
    struct tbv_dmabuf_post_group group = {.batch=&batch, .dev=&devices[0]};
    posts=0; fail_at=fail;
    for (unsigned i=0; i<4; i++) {
        rails[i].path.tx_ring = rails[i].path.rx_ring = &rings[i];
        rails[i].rail_id=i; rails[i].peer=&peer; rails[i].ready=ready;
        batch.lanes[i]=(struct tbv_dmabuf_lane){.rail=&rails[i],
            .dev=&devices[i%2], .count=i==0 ? a : i==2 ? b : 31,
            .frames=frames[i]};
        for (unsigned j=0; j<512; j++) frames[i][j].frame.index=j;
    }
    tbv_dmabuf_post_group(&group.work);
    assert(rings[1].next == 0 && rings[3].next == 0);
    assert(state.data_tx_posted == (tx ? posts : 0));
    assert(rails[0].path.data_tx_posted == (tx ? rings[0].next : 0));
    assert(rails[2].path.data_tx_posted == (tx ? rings[2].next : 0));
    if (!ready && a+b) { assert(group.status == -ENOTCONN && !posts); return; }
    if (fail < a+b) { assert(group.status == -EIO && posts == fail); return; }
    assert(group.status == 0 && posts == a+b);
    assert(rings[0].next == a && rings[2].next == b);
    /* Neither populated sibling may wait for the other's entire long batch. */
    if (a > 32 && b > 32) {
        unsigned mask=0;
        for (unsigned i=0; i<32; i++) mask |= 1u << seen[i];
        assert(mask == ((1u<<0) | (1u<<2)));
    }
}
int main(void) {
    unsigned lengths[] = {0,1,15,16,17,31,32,33,511,512};
    for (unsigned tx=0; tx<2; tx++)
        for (unsigned i=0; i<10; i++)
            for (unsigned j=0; j<10; j++) {
                check(tx,lengths[i],lengths[j],4096,true);
                check(tx,lengths[i],lengths[j],0,true);
                check(tx,lengths[i],lengths[j],17,true);
                check(tx,lengths[i],lengths[j],4096,false);
            }
    puts("Verbs posting: PASS (per-ring order, sibling progress, tails, NHI isolation, errors, counters)");
}
'''
run_c('verbs-post', shim + body + cases)
