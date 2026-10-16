#!/usr/bin/env python3
"""Check actual stop/free paths, including a tracker held by another link's RX."""
from smoke_support import ROOT, declaration, functions, run_c

source = (ROOT / "kernel/verbs_control.c").read_text()
layouts = "\n".join(declaration(source, rf"^struct {name} \{{[\s\S]*?^\}};")
                    for name in ("tbv_ctrl_frame", "tbv_ctrl_event", "tbv_control_ring", "tbv_control"))
body = functions(source, ("tbv_ctrl_release", "tbv_control_get", "tbv_control_put",
                          "tbv_ctrl_free", "tbv_control_stop", "tbv_control_destroy"))
shim = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uintptr_t dma_addr_t;
typedef unsigned int atomic_t;
typedef uint64_t atomic64_t;
typedef int spinlock_t;
typedef int wait_queue_head_t;
struct mutex { int unused; };
struct kref { int count; };
struct list_head { void *next, *prev; };
struct work_struct { int unused; };
struct delayed_work { int unused; };
struct ring_frame { int unused; };
struct device { int unused; };
struct tb_ring { int hop; };
struct tb_xdomain { int unused; };
struct tbv_control;
struct tbv_peer { struct tbv_control *dmabuf_control; struct tb_xdomain *xd; bool dmabuf_control_dead; struct mutex dmabuf_control_lock, control_lock; };
#define TBV_CTRL_TX_COUNT 32
#define TBV_CTRL_RX_COUNT 64
#define TBV_CTRL_FRAME_SIZE 256
#define CTRL_COUNTERS 15
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define WRITE_ONCE(value, next) ((value) = (next))
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define WARN_ON(value) assert(!(value))
static unsigned int xd_puts, dma_frees, ring_frees, ring_stops, drains, failures, disabled;
static void kfree(void *p) { free(p); }
static void tb_xdomain_put(struct tb_xdomain *xd) { xd_puts++; }
static void kref_get(struct kref *refs) { assert(refs->count > 0); refs->count++; }
static void kref_put(struct kref *refs, void (*release)(struct kref *)) { assert(refs->count > 0); if (!--refs->count) release(refs); }
static void tb_ring_stop(struct tb_ring *ring) { ring_stops++; }
static void cancel_work_sync(struct work_struct *work) { drains++; }
static void cancel_delayed_work_sync(struct delayed_work *work) { drains++; }
static void tb_xdomain_disable_paths(struct tb_xdomain *xd, int lp, int tx, int rp, int rx) { disabled++; }
static void tb_xdomain_release_in_hopid(struct tb_xdomain *xd, int hop) {}
static void tb_xdomain_release_out_hopid(struct tb_xdomain *xd, int hop) {}
static void tb_ring_free(struct tb_ring *ring) { ring_frees++; free(ring); }
static void dma_free_coherent(struct device *dev, size_t bytes, void *buf, dma_addr_t dma) { dma_frees++; free(buf); }
static void tbv_ctrl_fail(struct tbv_control *ctrl, int status);
"""
mocks = r"""
static void tbv_ctrl_fail(struct tbv_control *ctrl, int status) { failures++; ctrl->failure = status; ctrl->ready = false; }
static struct tbv_control *tracker(struct tbv_peer *peer, bool rings) {
    struct tbv_control *ctrl = calloc(1, sizeof(*ctrl)); assert(ctrl);
    ctrl->refs.count = 1; ctrl->peer = peer; ctrl->running = ctrl->ready = true;
    ctrl->local_path = -1; peer->dmabuf_control = ctrl;
    if (rings) {
        ctrl->ring = calloc(1, sizeof(*ctrl->ring)); assert(ctrl->ring);
        ctrl->ring->ctrl = ctrl;
        ctrl->ring->tx = calloc(1, sizeof(*ctrl->ring->tx)); assert(ctrl->ring->tx);
        ctrl->ring->rx = calloc(1, sizeof(*ctrl->ring->rx)); assert(ctrl->ring->rx);
        for (unsigned int i = 0; i < TBV_CTRL_TX_COUNT; i++) { ctrl->ring->tx_frames[i].buf = malloc(256); assert(ctrl->ring->tx_frames[i].buf); }
        for (unsigned int i = 0; i < TBV_CTRL_RX_COUNT; i++) { ctrl->ring->rx_frames[i].buf = malloc(256); assert(ctrl->ring->rx_frames[i].buf); }
        ctrl->enabled = true;
    } else ctrl->xdomain = true;
    return ctrl;
}
"""
cases = r"""
int main(void) {
    for (unsigned int rings = 0; rings < 2; rings++) {
        struct tbv_peer peer = {0};
        struct tbv_control *ctrl = tracker(&peer, rings);
        unsigned int before = xd_puts, before_stops = ring_stops, before_drains = drains;
        tbv_control_get(ctrl); // RX on another link retains the transaction tracker.
        tbv_control_stop(&peer); tbv_control_stop(&peer);
        assert(peer.dmabuf_control_dead && !ctrl->ready && !ctrl->running);
        assert(ring_stops == before_stops + 2 * rings && drains == before_drains + 2 * rings);
        tbv_control_destroy(&peer);
        assert(!peer.dmabuf_control && !ctrl->ring && ctrl->refs.count == 1);
        assert(xd_puts == before && ctrl->failure == -107);
        tbv_control_put(ctrl); assert(xd_puts == before + 1);
        tbv_control_destroy(&peer); // Idempotent on an unpublished tracker.
    }
    assert(dma_frees == 96 && ring_frees == 2 && disabled == 1 && failures == 2);
    // Failure during ring/storage allocation: cleanup accepts no/partial storage.
    struct tbv_peer peer = {0};
    struct tbv_control *ctrl = tracker(&peer, false);
    tbv_ctrl_free(ctrl);
    ctrl = tracker(&peer, false);
    ctrl->ring = calloc(1, sizeof(*ctrl->ring)); assert(ctrl->ring);
    ctrl->ring->tx = calloc(1, sizeof(*ctrl->ring->tx)); assert(ctrl->ring->tx);
    tbv_ctrl_free(ctrl);
    assert(ring_frees == 3 && xd_puts == 4);
    puts("Verbs control lifetime: PASS (XDomain/rings, stop/drain, RX-held tracker, partial allocation cleanup)");
    return 0;
}
"""
# Linux ENOTCONN differs from Darwin; use the Linux value in this kernel shim.
run_c("ctrl-lifetime", shim + "\n#define ENOTCONN 107\n" + layouts + mocks + body + cases)
