#!/usr/bin/env python3
"""Exercise actual RX transitions with simulated DMA and ownership callbacks."""
from smoke_support import ROOT, declaration, functions, run_c

source = (ROOT / "kernel/verbs_dmabuf.c").read_text()
state = declaration(source, r"^enum tbv_dmabuf_rx_state \{[\s\S]*?^\};")
body = functions(source, (
    "tbv_dmabuf_rx_active", "tbv_dmabuf_same", "tbv_dmabuf_rx_notify",
    "tbv_dmabuf_rx_finish", "tbv_dmabuf_rx_reap_impl", "tbv_dmabuf_rx_prepare",
    "tbv_dmabuf_control_request", "tbv_dmabuf_cancel_recv", "tbv_dmabuf_disconnect",
))
shim = r"""
#include <assert.h>
#include <stdio.h>
#include "proto/verbs_dmabuf.h"
#include "tools/ci/perf-noop.h"
typedef uint32_t u32;
typedef uint64_t u64;
struct work_struct { int unused; };
struct list_head { struct list_head *next; };
#define list_for_each_entry(pos, head, member) \
    for (struct list_head *n = (head)->next; n != (head) && \
         ((pos) = (void *)n, 1); n = n->next)
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define atomic_read(p) (*(p))
#define msecs_to_jiffies(ms) (ms)
#define dma_rmb() ((void)0)
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define mod_delayed_work(wq, work, delay) ((void)(delay))
static unsigned long jiffies;
static bool dmabuf_verbs = true;
static int begin_result, alloc_result, post_result, copy_result, finish_result;
static unsigned int begins, finishes, copies, drops, ref_gets, ref_puts, notifications;
static int notified;
struct tbv_rail { int refs; };
struct tbv_state { int lock; struct list_head peers; };
struct tbv_control { int refs; };
struct tbv_peer {
    struct list_head node;
    struct tbv_state *state;
    bool remote_host_known;
    unsigned char remote_host_id[16];
    u64 dmabuf_rx_token;
};
struct tbv_dmabuf_buffer {
    void *mr, *owner, *context;
    int (*finish)(void *, int);
    int (*copy)(void *, u32, void *, u32, bool);
};
struct tbv_dmabuf_lane {
    struct tbv_rail *rail;
    u32 first, count, staging_bytes;
    void *staging;
};
struct tbv_dmabuf_batch {
    struct tbv_dmabuf_lane lanes[TBV_DMABUF_MAX_RAILS];
    u32 count;
    int pending, error;
};
"""
rx = declaration(source, r"^struct tbv_dmabuf_rx \{[\s\S]*?^\};")
mocks = r"""
static struct { struct tbv_dmabuf_rx rx; int rx_lock, rx_watchdog; } dmabuf_engine;
static struct tbv_rail rail;
static bool lane_available = true;
static u64 tbv_dmabuf_clock(void) { return 0; }
enum { TBV_DMABUF_RX_BUILD, TBV_DMABUF_RX_POST };
static void tbv_dmabuf_record(int stage, u64 start) { (void)stage; (void)start; }
static void tbv_control_get(struct tbv_control *ctrl) { ref_gets++; ctrl->refs++; }
static void tbv_control_put(struct tbv_control *ctrl) { if (ctrl) { ref_puts++; ctrl->refs--; } }
static void tbv_control_done(struct tbv_control *ctrl, const struct tbv_dmabuf_wire *m, int status) {
    if (ctrl) { notifications++; notified = status; }
}
static int finish(void *ctx, int status) { finishes++; return status ?: finish_result; }
static int copy(void *ctx, u32 offset, void *data, u32 bytes, bool to_mr) {
    assert(to_mr); copies++; return copy_result;
}
static int tbv_ibdev_dmabuf_begin(struct tbv_state *s, struct tbv_rail *r,
                                const struct tbv_native_data_header *h,
                                struct tbv_dmabuf_buffer *buffer) {
    begins++;
    if (!begin_result)
        *buffer = (struct tbv_dmabuf_buffer){ .owner = &rail, .finish = finish, .copy = copy };
    return begin_result;
}
static struct tbv_rail *tbv_dmabuf_find_lane(struct tbv_state *s,
                                           const struct tbv_dmabuf_wire *m, u32 i) {
    if (!lane_available) return NULL;
    rail.refs++;
    return &rail;
}
static void tbv_rail_put(struct tbv_rail *r) { r->refs--; }
static void tbv_dmabuf_batch_drop(struct tbv_dmabuf_batch *batch, bool stop) {
    drops++;
    for (u32 i = 0; i < batch->count; i++) tbv_rail_put(batch->lanes[i].rail);
    memset(batch, 0, sizeof(*batch));
}
static int tbv_dmabuf_batch_alloc(struct tbv_dmabuf_batch *batch,
                                 const struct tbv_dmabuf_wire *m,
                                 const struct tbv_dmabuf_buffer *b, bool tx) {
    batch->pending = !!m->bytes;
    batch->lanes[0].count = !!m->bytes;
    batch->lanes[0].staging_bytes = m->bytes;
    return alloc_result;
}
static int tbv_dmabuf_batch_post(struct tbv_dmabuf_batch *b, bool tx) { return post_result; }
static int tbv_dmabuf_rx_reap(void);
"""
# The wrapper only records timings; tests exercise the actual reap implementation.
body = body.replace("static int tbv_dmabuf_rx_prepare", "static int tbv_dmabuf_rx_reap(void) { return tbv_dmabuf_rx_reap_impl(); }\n\nstatic int tbv_dmabuf_rx_prepare")
cases = r"""
int main(void) {
    struct tbv_state state = {0};
    struct tbv_peer peer = { .state = &state, .remote_host_known = true };
    struct tbv_control ctrl = { .refs = 1 };
    struct tbv_dmabuf_wire m = { .op = TBV_DMABUF_PREPARE, .token = 1,
        .count = 1, .bytes = 4096, .hdr.length = 8192 };
    peer.node.next = &state.peers; state.peers.next = &peer.node;
    assert(dmabuf_engine.rx.state == TBV_DMABUF_RX_IDLE);
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_READY);
    assert(begins == 1 && dmabuf_engine.rx.state == TBV_DMABUF_RX_ARMED);
    assert(ctrl.refs == 2 && ref_gets == 1);
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_READY);
    assert(begins == 1 && ref_gets == 1);
    struct tbv_dmabuf_wire other = m; other.token++;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &other) == TBV_DMABUF_BUSY);
    dmabuf_engine.rx.batch.pending = 0;
    assert(tbv_dmabuf_rx_reap_impl() == TBV_DMABUF_DONE);
    assert(dmabuf_engine.rx.state == TBV_DMABUF_RX_OPEN && !finishes);
    assert(ctrl.refs == 1 && notifications == 1 && notified == TBV_DMABUF_DONE);
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_DONE);
    m.offset = 4096;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_READY);
    dmabuf_engine.rx.batch.pending = 0;
    assert(tbv_dmabuf_rx_reap_impl() == TBV_DMABUF_DONE);
    assert(dmabuf_engine.rx.state == TBV_DMABUF_RX_CACHED && finishes == 1);
    assert(ctrl.refs == 1 && notifications == 2 && copies == 2 && !rail.refs);
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_DONE);
    assert(finishes == 1);
    m.offset = 0;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == -ESTALE);
    // Empty barrier finishes its WR directly in PREPARE, with no DONE reference.
    m.token++; m.bytes = m.hdr.length = 0;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_DONE);
    assert(finishes == 2 && notifications == 2 && ctrl.refs == 1);
    for (int error = 0; error < 5; error++) {
        m.token++; m.bytes = m.hdr.length = 4096;
        assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == TBV_DMABUF_READY);
        unsigned int before = finishes;
        if (error == 0) tbv_dmabuf_cancel_recv(&rail);
        else if (error == 1) tbv_dmabuf_disconnect(&rail);
        else if (error == 2) { dmabuf_engine.rx.batch.error = -EIO; assert(tbv_dmabuf_rx_reap_impl() == -EIO); }
        else if (error == 3) { copy_result = -EFAULT; dmabuf_engine.rx.batch.pending = 0; assert(tbv_dmabuf_rx_reap_impl() == -EFAULT); copy_result = 0; }
        else { finish_result = -ENOSPC; dmabuf_engine.rx.batch.pending = 0; assert(tbv_dmabuf_rx_reap_impl() == -ENOSPC); finish_result = 0; }
        assert(finishes == before + 1 && dmabuf_engine.rx.state == TBV_DMABUF_RX_CACHED);
        assert(ctrl.refs == 1 && !rail.refs && ref_gets == ref_puts);
        tbv_dmabuf_cancel_recv(&rail); assert(finishes == before + 1);
    }
    // Allocation/post failures finish a begun WR; missing lanes never begin one.
    m.token++; alloc_result = -ENOMEM;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == -ENOMEM);
    alloc_result = 0; m.token++; post_result = -EIO;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == -EIO);
    post_result = 0; m.token++; lane_available = false;
    unsigned int before = begins;
    assert(tbv_dmabuf_control_request(&peer, &ctrl, &m) == -ENOTCONN);
    assert(begins == before && !rail.refs && ctrl.refs == 1);
    puts("Verbs RX state: PASS (multi-batch, retry, cached result, empty barrier, errors, disconnect, ownership)");
    return 0;
}
"""
run_c("rx-state", shim + state + rx + mocks + body + cases)
