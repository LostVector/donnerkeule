// SPDX-License-Identifier: GPL-2.0
/* Verbs-owned DMA-BUF MRs and USB4 rendezvous. Payload never uses TCP.
 * The opt-in mode reserves the native rings for descriptors, in both
 * directions. Ordinary MRs use bounded coherent staging; DMA-BUF MRs do not.
 */
#define pr_fmt(fmt) "thunderbolt_ibverbs dmabuf: " fmt

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/thunderbolt.h>
#include <linux/timekeeping.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>

#include "tbv.h"
#include "verbs_dmabuf.h"
#include "verbs_control.h"
#include "perf.h"

static bool dmabuf_verbs;
module_param(dmabuf_verbs, bool, 0444);
MODULE_PARM_DESC(dmabuf_verbs,
	"Native descriptor verbs with DMA-BUF MRs and USB4 control; requires register_verbs=1");

static bool dmabuf_index = true;
module_param(dmabuf_index, bool, 0444);
MODULE_PARM_DESC(dmabuf_index, "Index DMA-BUF frame addresses once per NHI attachment");

static bool dmabuf_profile;
module_param(dmabuf_profile, bool, 0444);
MODULE_PARM_DESC(dmabuf_profile, "Collect descriptor verbs phase timings in debugfs summary");

static bool dmabuf_rx_eager = true;
module_param(dmabuf_rx_eager, bool, 0444);
MODULE_PARM_DESC(dmabuf_rx_eager, "Reap RX on the final DMA callback instead of waiting for STATUS");

static bool dmabuf_parallel_post = true;
module_param(dmabuf_parallel_post, bool, 0444);
MODULE_PARM_DESC(dmabuf_parallel_post, "Post large descriptor batches concurrently on distinct NHIs");

enum tbv_dmabuf_phase {
	TBV_DMABUF_QUEUE,
	TBV_DMABUF_JOB,
	TBV_DMABUF_TX_BUILD,
	TBV_DMABUF_PREPARE_TIME,
	TBV_DMABUF_TX_POST,
	TBV_DMABUF_TX_WAIT,
	TBV_DMABUF_STATUS_TIME,
	TBV_DMABUF_TX_RELEASE,
	TBV_DMABUF_TX_FINISH,
	TBV_DMABUF_RX_BUILD,
	TBV_DMABUF_RX_POST,
	TBV_DMABUF_RX_REAP,
	TBV_DMABUF_RX_RELEASE,
	TBV_DMABUF_PREPARE_WAIT,
	TBV_DMABUF_RNR_WAIT,
	TBV_DMABUF_STATUS_WAIT,
	TBV_DMABUF_DONE_WAIT,
	TBV_DMABUF_PHASE_COUNT,
};

static const char * const dmabuf_phase_names[] = {
	"queue", "job", "tx_build", "prepare", "tx_post", "tx_wait", "status",
	"tx_release", "tx_finish", "rx_build", "rx_post", "rx_reap", "rx_release",
	"prepare_wait", "rnr_wait", "status_wait", "done_wait",
};

static struct {
	atomic64_t calls;
	atomic64_t ns;
	atomic64_t max_ns;
} dmabuf_timings[TBV_DMABUF_PHASE_COUNT];

static u64 tbv_dmabuf_clock(void)
{
	return dmabuf_profile ? ktime_get_ns() : 0;
}

static void tbv_dmabuf_record(enum tbv_dmabuf_phase phase, u64 start)
{
	u64 ns, old;

	if (!start)
		return;
	ns = ktime_get_ns() - start;
	atomic64_inc(&dmabuf_timings[phase].calls);
	atomic64_add(ns, &dmabuf_timings[phase].ns);
	old = atomic64_read(&dmabuf_timings[phase].max_ns);
	while (old < ns) {
		u64 previous = atomic64_cmpxchg(&dmabuf_timings[phase].max_ns, old, ns);

		if (previous == old)
			break;
		old = previous;
	}
}

void tbv_dmabuf_debugfs_show(struct seq_file *s)
{
	u32 i;

	seq_printf(s, "dmabuf_verbs: %u\n", dmabuf_verbs);
	seq_printf(s, "dmabuf_index: %u\n", dmabuf_index);
	seq_printf(s, "dmabuf_profile: %u\n", dmabuf_profile);
	seq_printf(s, "dmabuf_rx_eager: %u\n", dmabuf_rx_eager);
	seq_printf(s, "dmabuf_parallel_post: %u\n", dmabuf_parallel_post);
	seq_printf(s, "dmabuf_control_ring: %u\n", tbv_control_enabled());
	seq_printf(s, "dmabuf_push_done: %u\n", tbv_control_push_enabled());
	if (!dmabuf_profile)
		return;
	for (i = 0; i < TBV_DMABUF_PHASE_COUNT; i++) {
		seq_printf(s, "dmabuf_%s_calls: %lld\n", dmabuf_phase_names[i],
			   atomic64_read(&dmabuf_timings[i].calls));
		seq_printf(s, "dmabuf_%s_ns: %lld\n", dmabuf_phase_names[i],
			   atomic64_read(&dmabuf_timings[i].ns));
		seq_printf(s, "dmabuf_%s_max_ns: %lld\n", dmabuf_phase_names[i],
			   atomic64_read(&dmabuf_timings[i].max_ns));
	}
}

struct tbv_dmabuf_page {
	dma_addr_t address;
	u32 bytes;
};

struct tbv_dmabuf_mapping {
	struct device *dev;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct tbv_dmabuf_page *pages;
};

struct tbv_dmabuf_mr {
	struct ib_umem_dmabuf *umem;
	struct mutex lock;
	u64 offset;
	u64 length;
	struct tbv_dmabuf_mapping maps[TBV_DMABUF_MAX_RAILS];
};

struct tbv_dmabuf_batch;

struct tbv_dmabuf_frame {
	struct ring_frame frame;
	struct tbv_dmabuf_batch *batch;
	struct tbv_dmabuf_lane *lane;
	u32 bytes;
	u8 eof;
};

struct tbv_dmabuf_lane {
	struct tbv_rail *rail;
	struct tbv_dmabuf_frame *frames;
	struct device *dev;
	void *staging;
	dma_addr_t staging_dma;
	u32 first;
	u32 count;
	u32 staging_bytes;
	struct tbv_perf_id perf;
	u64 perf_post_started;
	atomic_t perf_pending;
	bool perf_active;
};

struct tbv_dmabuf_post_group {
	struct work_struct work;
	struct tbv_dmabuf_batch *batch;
	struct device *dev;
	int status;
	u64 perf_queued_ns;
};

struct tbv_dmabuf_batch {
	struct tbv_dmabuf_lane lanes[TBV_DMABUF_MAX_RAILS];
	struct tbv_dmabuf_post_group posts[TBV_DMABUF_MAX_RAILS];
	atomic_t pending;
	atomic_t error;
	wait_queue_head_t wait;
	u32 count;
	bool submitted;
	bool tx;
	struct tbv_perf_id perf;
	u64 perf_dma_done_ns;
};

struct tbv_dmabuf_job {
	struct work_struct work;
	struct tbv_state *state;
	struct tbv_rail *route;
	struct tbv_native_data_header hdr;
	struct tbv_dmabuf_buffer buffer;
	u8 retries;
	u8 rnr_retries;
	unsigned long timeout;
	u64 queued_ns;
	u64 perf_queued_ns;
	struct tbv_perf_id perf;
};

/* OPEN retains the WR between DMA batches; CACHED retains its final result.
 * All transitions and the buffer/control reference are protected by rx_lock. */
enum tbv_dmabuf_rx_state {
	TBV_DMABUF_RX_IDLE,
	TBV_DMABUF_RX_OPEN,
	TBV_DMABUF_RX_ARMED,
	TBV_DMABUF_RX_CACHED,
};

struct tbv_dmabuf_rx {
	struct tbv_dmabuf_wire request;
	struct tbv_dmabuf_buffer buffer;
	struct tbv_dmabuf_batch batch;
	struct tbv_control *control;
	unsigned long deadline;
	u32 next_offset;
	int status;
	enum tbv_dmabuf_rx_state state;
};

/* One ordered TX stream and one RX transaction span all rails/links.
 * ring_lock serializes ring replacement; tx_lock protects cancellation. */
struct tbv_dmabuf_engine {
	struct workqueue_struct *tx_wq, *post_wq;
	struct mutex rx_lock, ring_lock, tx_lock;
	void *active_context;
	struct tbv_dmabuf_batch *active_batch;
	struct tbv_dmabuf_rx rx;
	struct delayed_work rx_watchdog;
	struct work_struct rx_complete_work;
	atomic64_t token;
};

static struct tbv_dmabuf_engine dmabuf_engine = {
	.rx_lock = __MUTEX_INITIALIZER(dmabuf_engine.rx_lock),
	.ring_lock = __MUTEX_INITIALIZER(dmabuf_engine.ring_lock),
	.tx_lock = __MUTEX_INITIALIZER(dmabuf_engine.tx_lock),
	.token = ATOMIC64_INIT(0),
};

static bool tbv_dmabuf_rx_active(const struct tbv_dmabuf_rx *rx)
{
	return rx->state == TBV_DMABUF_RX_OPEN || rx->state == TBV_DMABUF_RX_ARMED;
}

bool tbv_dmabuf_verbs_enabled(void)
{
	return dmabuf_verbs;
}

struct tbv_dmabuf_mr *tbv_dmabuf_import(struct ib_device *device, u64 offset,
				       u64 length, int fd, int access)
{
	struct tbv_dmabuf_mr *mr;
	u64 end;

	if (!dmabuf_verbs)
		return ERR_PTR(-EOPNOTSUPP);
	if (!length || length > (256ull << 20) || length % PAGE_SIZE || offset % PAGE_SIZE ||
	    check_add_overflow(offset, length, &end) || end > ULONG_MAX)
		return ERR_PTR(-EINVAL);
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);
	/* The RDMA core handles fd/range validation and exporter pinning. This
	 * mapping is only the lifetime anchor; descriptor IOVAs always come
	 * from an attachment to the actual NHI device below. */
	mr->umem = ib_umem_dmabuf_get_pinned(device, offset, length, fd, access);
	if (IS_ERR(mr->umem)) {
		int ret = PTR_ERR(mr->umem);

		kfree(mr);
		return ERR_PTR(ret);
	}
	if (mr->umem->attach->dmabuf->size > (256ull << 20)) {
		ib_umem_release(&mr->umem->umem);
		kfree(mr);
		return ERR_PTR(-E2BIG);
	}
	mutex_init(&mr->lock);
	mr->offset = offset;
	mr->length = length;
	return mr;
}

void tbv_dmabuf_release(struct tbv_dmabuf_mr *mr)
{
	u32 i;

	if (!mr)
		return;
	for (i = 0; i < TBV_DMABUF_MAX_RAILS; i++) {
		struct tbv_dmabuf_mapping *map = &mr->maps[i];

		kvfree(map->pages);
		if (map->sgt)
			dma_buf_unmap_attachment_unlocked(map->attach, map->sgt,
							 DMA_BIDIRECTIONAL);
		if (map->attach)
			dma_buf_detach(map->attach->dmabuf, map->attach);
		put_device(map->dev);
	}
	ib_umem_release(&mr->umem->umem);
	mutex_destroy(&mr->lock);
	kfree(mr);
}

/* Entries retain each SG boundary: indexing never makes an NHI descriptor
 * span two entries. The padded receive span is checked by the same lookup. */
static int tbv_dmabuf_index_map(struct tbv_dmabuf_mr *mr,
				struct tbv_dmabuf_mapping *map)
{
	struct scatterlist *sg;
	u64 skip = mr->offset;
	u32 count = mr->length / TBV_DMABUF_FRAME_BYTES;
	u32 page = 0, i;

	map->pages = kvcalloc(count, sizeof(*map->pages), GFP_KERNEL);
	if (!map->pages)
		return -ENOMEM;
	for_each_sgtable_dma_sg(map->sgt, sg, i) {
		u32 len = sg_dma_len(sg);

		if (skip >= len) {
			skip -= len;
			continue;
		}
		while (page < count && skip < len) {
			struct tbv_dmabuf_page *entry = &map->pages[page++];
			u64 address;

			entry->bytes = min_t(u64, TBV_DMABUF_FRAME_BYTES, len - skip);
			if (check_add_overflow((u64)sg_dma_address(sg), skip, &address) ||
			    address > ~(dma_addr_t)0 ||
			    entry->bytes - 1 > ~(dma_addr_t)0 - address)
				return -EFAULT;
			entry->address = address;
			skip += TBV_DMABUF_FRAME_BYTES;
		}
		if (page == count)
			return 0;
		skip -= len;
	}
	return -EFAULT;
}

static int tbv_dmabuf_address(struct tbv_dmabuf_mr *mr, struct device *dev,
			      u64 offset, u32 bytes, dma_addr_t *address)
{
	struct tbv_dmabuf_mapping *map = NULL;
	struct scatterlist *sg;
	u64 skip;
	u64 perf_attach = 0;
	u32 i;
	int ret = -EFAULT;

	if (!bytes || offset % TBV_DMABUF_FRAME_BYTES ||
	    offset >= mr->length || bytes > mr->length - offset ||
	    check_add_overflow(offset, mr->offset, &skip))
		return -EFAULT;
	mutex_lock(&mr->lock);
	for (i = 0; i < TBV_DMABUF_MAX_RAILS; i++)
		if (mr->maps[i].dev == dev) {
			map = &mr->maps[i];
			break;
		}
	if (!map) {
		perf_attach = tbv_perf_clock();
		for (i = 0; i < TBV_DMABUF_MAX_RAILS; i++)
			if (!mr->maps[i].dev) {
				map = &mr->maps[i];
				break;
			}
		if (!map) {
			ret = -ENOSPC;
			goto out;
		}
		map->attach = dma_buf_attach(mr->umem->attach->dmabuf, dev);
		if (IS_ERR(map->attach)) {
			ret = PTR_ERR(map->attach);
			map->attach = NULL;
			goto out;
		}
		map->sgt = dma_buf_map_attachment_unlocked(map->attach,
							  DMA_BIDIRECTIONAL);
		if (IS_ERR(map->sgt)) {
			ret = PTR_ERR(map->sgt);
			map->sgt = NULL;
			dma_buf_detach(map->attach->dmabuf, map->attach);
			map->attach = NULL;
			goto out;
		}
		map->dev = get_device(dev);
		if (dmabuf_index) {
			ret = tbv_dmabuf_index_map(mr, map);
			if (ret) {
				kvfree(map->pages);
				map->pages = NULL;
				dma_buf_unmap_attachment_unlocked(map->attach, map->sgt,
							 DMA_BIDIRECTIONAL);
				dma_buf_detach(map->attach->dmabuf, map->attach);
				put_device(map->dev);
				memset(map, 0, sizeof(*map));
				goto out;
			}
		}
	}
	if (map->pages) {
		struct tbv_dmabuf_page *page = &map->pages[offset / TBV_DMABUF_FRAME_BYTES];

		if (bytes <= page->bytes) {
			*address = page->address;
			ret = 0;
		}
		goto out;
	}
	for_each_sgtable_dma_sg(map->sgt, sg, i) {
		u64 a;

		if (skip >= sg_dma_len(sg)) {
			skip -= sg_dma_len(sg);
			continue;
		}
		/* An NHI descriptor cannot cross an SG entry. */
		if (bytes > sg_dma_len(sg) - skip ||
		    check_add_overflow((u64)sg_dma_address(sg), skip, &a) ||
		    a > ~(dma_addr_t)0 || bytes - 1 > ~(dma_addr_t)0 - a)
			break;
		*address = a;
		ret = 0;
		break;
	}
out:
	mutex_unlock(&mr->lock);
	if (perf_attach) {
		struct tbv_perf_id id = { .bytes = mr->length };

		tbv_perf_span(&id, "mr_attach_index", perf_attach, ret, dmabuf_index);
	}
	return ret;
}

/* Raw frames have no operation identifier. A failed batch quarantines both
 * directions until the physical connection/module is recreated. Draining
 * precedes every release of frame metadata, mappings and verbs references. */
static void tbv_dmabuf_stop_rail(struct tbv_rail *rail)
{
	mutex_lock(&dmabuf_engine.ring_lock);
	if (!READ_ONCE(rail->gpu_direct_used)) {
		WRITE_ONCE(rail->gpu_direct_used, true);
		tb_ring_stop(rail->path.tx_ring);
		tb_ring_stop(rail->path.rx_ring);
	}
	mutex_unlock(&dmabuf_engine.ring_lock);
}

/* Reboot/poweroff: stop the rail's data rings before the NHI's own shutdown
 * removes the domain. Reuses the gpu_direct_used marker, so tbv_path_destroy()
 * does not stop them a second time (tb_ring_stop() warns on a stopped ring).
 * Bounded: a batch holding ring_lock must not be able to stall shutdown. */
int tbv_dmabuf_quiesce_rail(struct tbv_rail *rail)
{
	int tries = 1000;

	if (!rail->path.tx_ring || !rail->path.rx_ring ||
	    (rail->path.state != TBV_PATH_RING_STARTED &&
	     rail->path.state != TBV_PATH_TUNNEL_ENABLED))
		return 0;
	while (!mutex_trylock(&dmabuf_engine.ring_lock)) {
		if (!--tries)
			return -EBUSY;
		msleep(1);
	}
	if (!READ_ONCE(rail->gpu_direct_used)) {
		WRITE_ONCE(rail->gpu_direct_used, true);
		tb_ring_stop(rail->path.tx_ring);
		tb_ring_stop(rail->path.rx_ring);
	}
	mutex_unlock(&dmabuf_engine.ring_lock);
	return 0;
}

static void tbv_dmabuf_batch_drop(struct tbv_dmabuf_batch *batch, bool abort)
{
	u64 start = tbv_dmabuf_clock();
	u64 perf_start = tbv_perf_clock();
	struct tbv_perf_id perf = batch->perf;
	bool tx = batch->tx;
	u32 i;

	for (i = 0; i < batch->count; i++) {
		struct tbv_dmabuf_lane *lane = &batch->lanes[i];

		if (batch->submitted && lane->rail) {
			if (abort) {
				tbv_dmabuf_stop_rail(lane->rail);
			} else {
				struct tb_ring *ring = batch->tx ? lane->rail->path.tx_ring :
								 lane->rail->path.rx_ring;

				/* A zero pending count precedes the callback's last
				 * wakeup. Wait for its return before freeing its frame
				 * or the batch waitqueue, including on success. */
				flush_work(&ring->work);
			}
		}
		if (lane->staging)
			dma_free_coherent(lane->dev, lane->staging_bytes,
					  lane->staging, lane->staging_dma);
		kfree(lane->frames);
		if (lane->rail)
			tbv_rail_put(lane->rail);
	}
	memset(batch, 0, sizeof(*batch));
	tbv_dmabuf_record(tx ? TBV_DMABUF_TX_RELEASE : TBV_DMABUF_RX_RELEASE, start);
	tbv_perf_span(&perf, "batch_release", perf_start, abort ? -ECANCELED : 0, 0);
}

void tbv_dmabuf_cancel(void *context)
{
	u32 i;

	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_engine.tx_lock);
	if (dmabuf_engine.active_context == context && dmabuf_engine.active_batch)
		for (i = 0; i < dmabuf_engine.active_batch->count; i++)
			tbv_dmabuf_stop_rail(dmabuf_engine.active_batch->lanes[i].rail);
	mutex_unlock(&dmabuf_engine.tx_lock);
}

static bool tbv_dmabuf_rail_ready(struct tbv_rail *rail)
{
	return rail->peer->backend == TBV_BACKEND_NATIVE &&
		!READ_ONCE(rail->removing) && !READ_ONCE(rail->gpu_direct_used) &&
		rail->peer->remote_host_known && tbv_rail_data_ready(rail) &&
		(!tbv_control_required(rail->peer) || tbv_control_ready(rail->peer));
}

static void tbv_dmabuf_complete(struct tb_ring *ring, struct ring_frame *frame,
				bool canceled)
{
	struct tbv_dmabuf_frame *f = container_of(frame, struct tbv_dmabuf_frame,
						  frame);
	struct tbv_path *path = &f->lane->rail->path;
	struct tbv_state *state = path->rail->peer->state;
	int error = 0;

	/* A one-packet short frame carries only its EOF PDF and reports SOF=0.
	 * Its exact byte count, EOF and CRC are still checked below. */
	if (canceled)
		error = -ECANCELED;
	else if (!ring->is_tx &&
		 ((frame->size ?: TBV_DMABUF_FRAME_BYTES) != f->bytes ||
		  (frame->sof != TBV_DATA_PDF_FRAME_START &&
		   (f->bytes > 256 || frame->sof)) || frame->eof != f->eof ||
		  frame->flags & (RING_DESC_CRC_ERROR | RING_DESC_BUFFER_OVERRUN))) {
		error = -EBADMSG;
		pr_debug("RX mismatch size=%u expected=%u sof=%u eof=%u expected_eof=%u flags=%x\n",
			 frame->size, f->bytes, frame->sof, frame->eof, f->eof, frame->flags);
	}
	if (error)
		atomic_cmpxchg(&f->batch->error, 0, error);
#ifndef TBV_DISABLE_PROFILING
	if (f->lane->perf_active) {
		int left = atomic_dec_return(&f->lane->perf_pending);

		if (left == (int)f->lane->count - 1)
			tbv_perf_mark(&f->lane->perf, "dma_first", error, left);
		if (!left)
			tbv_perf_mark(&f->lane->perf, "dma_last", error, 0);
	}
#endif
	if (canceled) {
		atomic64_inc(ring->is_tx ? &state->data_tx_canceled :
					 &state->data_rx_canceled);
	} else if (ring->is_tx) {
		atomic64_inc(&state->data_tx_completed);
		atomic64_inc(&path->data_tx_completed);
		atomic64_add(f->bytes, &path->tx_bytes);
	} else {
		atomic64_inc(&state->data_rx_completed);
		atomic64_inc(&path->data_rx_completed);
		atomic64_add(f->bytes, &path->rx_bytes);
		if (error)
			atomic64_inc(&state->data_rx_bad_frame);
	}
	if (atomic_dec_and_test(&f->batch->pending) || error) {
#ifndef TBV_DISABLE_PROFILING
		if (!atomic_read(&f->batch->pending)) {
			WRITE_ONCE(f->batch->perf_dma_done_ns, tbv_perf_clock());
			tbv_perf_mark(&f->batch->perf, "batch_dma_done", error, 0);
		}
#endif
		wake_up_all(&f->batch->wait);
		/* Reaping may drain ring callbacks and take the RX mutex. Keep it
		 * off this callback's ring worker; STATUS still confirms placement. */
		if (!ring->is_tx && (dmabuf_rx_eager || tbv_control_enabled() ||
				     tbv_control_push_enabled()))
			queue_work(system_highpri_wq, &dmabuf_engine.rx_complete_work);
	}
}

static int tbv_dmabuf_batch_alloc_impl(struct tbv_dmabuf_batch *batch,
				  const struct tbv_dmabuf_wire *m,
				  const struct tbv_dmabuf_buffer *buffer, bool tx)
{
	u32 frames = DIV_ROUND_UP(m->bytes, TBV_DMABUF_FRAME_BYTES);
	u32 i;

	atomic_set(&batch->pending, frames);
	atomic_set(&batch->error, 0);
	init_waitqueue_head(&batch->wait);
	batch->tx = tx;
	for (i = 0; i < batch->count; i++) {
		struct tbv_dmabuf_lane *lane = &batch->lanes[i];
		struct tb_ring *ring = tx ? lane->rail->path.tx_ring :
					   lane->rail->path.rx_ring;
		u32 end = DIV_ROUND_UP((u64)frames * (i + 1), batch->count);
		u32 n;
		u64 perf_start;

		lane->first = DIV_ROUND_UP((u64)frames * i, batch->count);
		lane->count = end - lane->first;
		lane->dev = tb_ring_dma_device(ring);
		lane->perf = batch->perf;
		/* rail_id is local to a link; the wire lane is unique across links. */
		lane->perf.rail = i;
		lane->perf_active = !!tbv_perf_clock();
		if (lane->perf_active)
			atomic_set(&lane->perf_pending, lane->count);
		if (lane->count >= ring->size || !tbv_dma_device_ready(lane->dev))
			return -ENODEV;
		if (!lane->count)
			continue;
		perf_start = tbv_perf_clock();
		lane->frames = kcalloc(lane->count, sizeof(*lane->frames), GFP_KERNEL);
		if (!lane->frames)
			return -ENOMEM;
		if (!buffer->mr) {
			lane->staging_bytes = lane->count * TBV_DMABUF_FRAME_BYTES;
			lane->staging = dma_alloc_coherent(lane->dev, lane->staging_bytes,
							   &lane->staging_dma, GFP_KERNEL);
			if (!lane->staging)
				return -ENOMEM;
		}
		tbv_perf_span(&lane->perf, "lane_alloc", perf_start, 0, lane->count);
		perf_start = tbv_perf_clock();
		for (n = 0; n < lane->count; n++) {
			struct tbv_dmabuf_frame *f = &lane->frames[n];
			u32 offset = (lane->first + n) * TBV_DMABUF_FRAME_BYTES;
			int ret;

			INIT_LIST_HEAD(&f->frame.list);
			f->batch = batch;
			f->lane = lane;
			f->bytes = min_t(u32, TBV_DMABUF_FRAME_BYTES, m->bytes - offset);
			/* The per-rail last marker detects a lost interior frame;
			 * counting completions alone could accept shifted payload. */
			f->eof = n + 1 == lane->count ? TBV_DATA_PDF_FRAME_END : 2;
			f->frame.size = f->bytes % TBV_DMABUF_FRAME_BYTES;
			f->frame.sof = TBV_DATA_PDF_FRAME_START;
			f->frame.eof = f->eof;
			f->frame.callback = tbv_dmabuf_complete;
			if (buffer->mr) {
				ret = tbv_dmabuf_address(buffer->mr, lane->dev,
					buffer->offset + m->offset + offset,
					tx ? f->bytes : TBV_DMABUF_FRAME_BYTES,
					&f->frame.buffer_phy);
				if (ret)
					return ret;
			} else {
				f->frame.buffer_phy = lane->staging_dma +
					n * TBV_DMABUF_FRAME_BYTES;
				if (tx) {
					ret = buffer->copy(buffer->context, m->offset + offset,
						lane->staging + n * TBV_DMABUF_FRAME_BYTES,
						f->bytes, false);
					if (ret)
						return ret;
				}
			}
		}
		tbv_perf_span(&lane->perf, "lane_build", perf_start, 0, lane->count);
	}
	return 0;
}

static int tbv_dmabuf_batch_alloc(struct tbv_dmabuf_batch *batch,
				  const struct tbv_dmabuf_wire *m,
				  const struct tbv_dmabuf_buffer *buffer, bool tx)
{
	u64 start = tbv_perf_clock();
	int ret;

	if (start) {
		batch->perf = tbv_perf_wire_id(m, tx);
		batch->perf.wr_id = buffer->wr_id;
	}
	ret = tbv_dmabuf_batch_alloc_impl(batch, m, buffer, tx);
	tbv_perf_span(&batch->perf, "batch_build", start, ret, buffer->mr != NULL);
	return ret;
}

static int tbv_dmabuf_lane_post(struct tbv_dmabuf_lane *lane, bool tx,
			       u32 first, u32 count)
{
	struct tb_ring *ring = tx ? lane->rail->path.tx_ring : lane->rail->path.rx_ring;
	u64 start = tbv_perf_clock();
	u32 n;
	int ret = 0;

	if (!first)
		lane->perf_post_started = start;
	if (!tbv_dmabuf_rail_ready(lane->rail)) {
		ret = -ENOTCONN;
		goto out;
	}
	for (n = first; n < first + count; n++) {
		ret = tx ? tb_ring_tx(ring, &lane->frames[n].frame) :
			   tb_ring_rx(ring, &lane->frames[n].frame);
		if (ret)
			goto out;
		if (tx) {
			atomic64_inc(&lane->rail->peer->state->data_tx_posted);
			atomic64_inc(&lane->rail->path.data_tx_posted);
		}
	}
out:
	tbv_perf_span(&lane->perf, "lane_post_chunk", start, ret, count);
	if (ret || first + count == lane->count)
		tbv_perf_span(&lane->perf, "lane_post", lane->perf_post_started,
			      ret, first + count);
	return ret;
}

static void tbv_dmabuf_post_group(struct work_struct *work)
{
	struct tbv_dmabuf_post_group *group = container_of(work,
						struct tbv_dmabuf_post_group, work);
	u64 started = tbv_perf_clock();
	struct tbv_perf_id perf = group->batch->perf;
	u32 i, first, frames = 0;

	/* A group is one NHI; its first nonempty rail labels the worker. */
	for (i = 0; i < group->batch->count; i++)
		if (group->batch->lanes[i].dev == group->dev &&
		    group->batch->lanes[i].count) {
			perf.rail = group->batch->lanes[i].perf.rail;
			break;
		}
	tbv_perf_interval(&perf, "post_queue", group->perf_queued_ns, started, 0, 0);

	for (i = 0; i < group->batch->count; i++)
		if (group->batch->lanes[i].dev == group->dev)
			frames = max(frames, group->batch->lanes[i].count);
	/* Start every rail before filling any one ring. Posting a whole rail
	 * first leaves its sibling idle during hundreds of MMIO writes. Small
	 * turns overlap their DMA while preserving each ring's frame order. */
	for (first = 0; first < frames; first += 16) {
		for (i = 0; i < group->batch->count; i++) {
			struct tbv_dmabuf_lane *lane = &group->batch->lanes[i];

			if (lane->dev != group->dev || first >= lane->count)
				continue;
			group->status = tbv_dmabuf_lane_post(lane, group->batch->tx,
				first, min_t(u32, 16, lane->count - first));
			if (group->status)
				goto out;
		}
	}
out:
	tbv_perf_span(&perf, "post_worker", started, group->status, 0);
}

static int tbv_dmabuf_batch_post(struct tbv_dmabuf_batch *batch, bool tx)
{
	u32 i, groups = 0, frames = 0;
	int ret = 0;

	for (i = 0; i < batch->count; i++) {
		struct tbv_dmabuf_lane *lane = &batch->lanes[i];
		u32 j;

		frames += lane->count;
		if (!lane->count)
			continue;
		for (j = 0; j < groups; j++)
			if (batch->posts[j].dev == lane->dev)
				break;
		if (j == groups) {
			batch->posts[groups].dev = lane->dev;
			batch->posts[groups].batch = batch;
			groups++;
		}
	}
	if (!frames)
		return 0;
	batch->submitted = true;
	if (!dmabuf_parallel_post || groups < 2 || frames < 128) {
		for (i = 0; i < batch->count; i++) {
			ret = tbv_dmabuf_lane_post(&batch->lanes[i], tx, 0,
						 batch->lanes[i].count);
			if (ret)
				return ret;
		}
		return 0;
	}
	/* Lanes sharing an NHI remain sequential: its MMIO/locks are shared.
	 * Different NHIs can prepare descriptors together. Every worker returns
	 * before any batch metadata can be drained or released, including errors. */
	for (i = 0; i < groups; i++) {
		INIT_WORK(&batch->posts[i].work, tbv_dmabuf_post_group);
		batch->posts[i].perf_queued_ns = tbv_perf_clock();
		queue_work(dmabuf_engine.post_wq, &batch->posts[i].work);
	}
	for (i = 0; i < groups; i++) {
		flush_work(&batch->posts[i].work);
		if (!ret)
			ret = batch->posts[i].status;
	}
	return ret;
}

static void tbv_dmabuf_batch_kick(struct tbv_dmabuf_batch *batch, bool tx)
{
	u32 i;

	tbv_perf_mark(&batch->perf, "dma_watchdog", 0, atomic_read(&batch->pending));

	for (i = 0; i < batch->count; i++) {
		struct tbv_rail *rail = batch->lanes[i].rail;
		struct tb_ring *ring = tx ? rail->path.tx_ring : rail->path.rx_ring;

		mutex_lock(&dmabuf_engine.ring_lock);
		if (!READ_ONCE(rail->gpu_direct_used))
			schedule_work(&ring->work);
		mutex_unlock(&dmabuf_engine.ring_lock);
	}
}

static int tbv_dmabuf_batch_wait(struct tbv_dmabuf_batch *batch,
				 const struct tbv_dmabuf_buffer *buffer,
				 unsigned long deadline)
{
	while (atomic_read(&batch->pending) && !atomic_read(&batch->error)) {
		if (!buffer->alive(buffer->context))
			return -ECANCELED;
		if (time_after_eq(jiffies, deadline))
			return -ETIMEDOUT;
		/*
		 * Completion callbacks wake us directly. Only inspect the rings
		 * after a missed notification while DMA is still outstanding.
		 */
		if (!wait_event_timeout(batch->wait, !atomic_read(&batch->pending) ||
				atomic_read(&batch->error), msecs_to_jiffies(10)) &&
		    atomic_read(&batch->pending) && !atomic_read(&batch->error))
			tbv_dmabuf_batch_kick(batch, true);
	}
	return atomic_read(&batch->error);
}

static bool tbv_dmabuf_same(const struct tbv_dmabuf_wire *a,
			    const struct tbv_dmabuf_wire *b, bool batch)
{
	return tbv_dmabuf_wire_same(a, b, batch);
}

static void tbv_dmabuf_rx_notify(int status)
{
	struct tbv_control *ctrl = dmabuf_engine.rx.control;
	u64 start = tbv_perf_clock();

	dmabuf_engine.rx.control = NULL;
	tbv_control_done(ctrl, &dmabuf_engine.rx.request, status);
	tbv_perf_wire(&dmabuf_engine.rx.request, false, "done_send", start, status,
		      ctrl != NULL);
	tbv_control_put(ctrl);
}

static void tbv_dmabuf_rx_finish(int status)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_engine.rx;
	u64 start;

	tbv_dmabuf_batch_drop(&rx->batch, !!status);
	if (rx->state == TBV_DMABUF_RX_ARMED)
		rx->state = TBV_DMABUF_RX_OPEN;
	start = tbv_perf_clock();
	if (tbv_dmabuf_rx_active(rx))
		status = rx->buffer.finish(rx->buffer.context, status);
	tbv_perf_wire(&rx->request, false, "rx_finish", start, status, 0);
	memset(&rx->buffer, 0, sizeof(rx->buffer));
	rx->state = TBV_DMABUF_RX_CACHED;
	rx->status = status;
	tbv_dmabuf_rx_notify(status ?: TBV_DMABUF_DONE);
}

void tbv_dmabuf_cancel_recv(void *owner)
{
	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_engine.rx_lock);
	if (tbv_dmabuf_rx_active(&dmabuf_engine.rx) && dmabuf_engine.rx.buffer.owner == owner)
		tbv_dmabuf_rx_finish(-ECANCELED);
	mutex_unlock(&dmabuf_engine.rx_lock);
}

static int tbv_dmabuf_rx_reap_impl(void)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_engine.rx;
	u32 i;
	int ret = atomic_read(&rx->batch.error);

	if (rx->state != TBV_DMABUF_RX_ARMED)
		return TBV_DMABUF_DONE;
	if (ret)
		goto fail;
	if (atomic_read(&rx->batch.pending))
		return TBV_DMABUF_BUSY;
	dma_rmb();
	if (!rx->buffer.mr) {
		for (i = 0; i < rx->batch.count; i++) {
			struct tbv_dmabuf_lane *lane = &rx->batch.lanes[i];
			u32 offset = lane->first * TBV_DMABUF_FRAME_BYTES;
			u32 bytes = min_t(u32, lane->staging_bytes, rx->request.bytes - offset);

			if (!lane->count)
				continue;
			ret = rx->buffer.copy(rx->buffer.context,
				rx->request.offset + offset, lane->staging, bytes, true);
			if (ret)
				goto fail;
		}
	}
	rx->next_offset = rx->request.offset + rx->request.bytes;
	if (rx->next_offset == rx->request.hdr.length) {
		tbv_dmabuf_rx_finish(0);
		return rx->status ?: TBV_DMABUF_DONE;
	} else {
		tbv_dmabuf_batch_drop(&rx->batch, false);
		rx->state = TBV_DMABUF_RX_OPEN;
		tbv_dmabuf_rx_notify(TBV_DMABUF_DONE);
	}
	return TBV_DMABUF_DONE;
fail:
	tbv_dmabuf_rx_finish(ret);
	return ret;
}

static int tbv_dmabuf_rx_reap(void)
{
	u64 start = tbv_dmabuf_clock();
	u64 perf_start = tbv_perf_clock();
	int ret = tbv_dmabuf_rx_reap_impl();

	tbv_dmabuf_record(TBV_DMABUF_RX_REAP, start);
	tbv_perf_wire(&dmabuf_engine.rx.request, false, "rx_reap", perf_start, ret, 0);
	return ret;
}

static void tbv_dmabuf_rx_completed(struct work_struct *work)
{
	u64 start = tbv_perf_clock();

	mutex_lock(&dmabuf_engine.rx_lock);
	tbv_perf_wire(&dmabuf_engine.rx.request, false, "rx_worker_lock", start, 0, 0);
	tbv_perf_interval(&dmabuf_engine.rx.batch.perf, "rx_callback_worker",
		READ_ONCE(dmabuf_engine.rx.batch.perf_dma_done_ns), start, 0, 0);
	if (tbv_dmabuf_rx_active(&dmabuf_engine.rx) && dmabuf_engine.rx.state == TBV_DMABUF_RX_ARMED)
		tbv_dmabuf_rx_reap();
	mutex_unlock(&dmabuf_engine.rx_lock);
}

static struct tbv_rail *tbv_dmabuf_find_lane(struct tbv_state *state,
					    const struct tbv_dmabuf_wire *m, u32 index)
{
	struct tbv_peer *peer;
	struct tbv_rail *found = NULL;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (!peer->xd->remote_uuid || !peer->remote_host_known ||
		    memcmp(peer->remote_host_id, m->src_host, 16) ||
		    memcmp(peer->xd->remote_uuid, m->lanes[index].domain, 16))
			continue;
		list_for_each_entry(rail, &peer->rails, node) {
			if (!tbv_dmabuf_rail_ready(rail) ||
			    rail->remote_transmit_path != m->lanes[index].path)
				continue;
			refcount_inc(&rail->refcnt);
			found = rail;
			goto out;
		}
	}
out:
	mutex_unlock(&state->lock);
	return found;
}

static int tbv_dmabuf_rx_prepare(struct tbv_state *state,
				 const struct tbv_dmabuf_wire *m)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_engine.rx;
	u64 start;
	u64 perf_start = tbv_perf_clock();
	u32 i, j;
	int ret;

	if (rx->state == TBV_DMABUF_RX_CACHED && tbv_dmabuf_same(&rx->request, m, true))
		return rx->status ?: TBV_DMABUF_DONE;
	if (tbv_dmabuf_rx_active(rx)) {
		if (!tbv_dmabuf_same(&rx->request, m, false))
			return TBV_DMABUF_BUSY;
		if (rx->state == TBV_DMABUF_RX_ARMED)
			return tbv_dmabuf_same(&rx->request, m, true) ?
				TBV_DMABUF_READY : -EINVAL;
		if (m->offset == rx->request.offset && m->bytes == rx->request.bytes)
			return TBV_DMABUF_DONE;
		if (m->offset != rx->next_offset)
			return -EINVAL;
	} else if (m->offset) {
		return -EINVAL;
	}
	if (!tbv_dmabuf_rx_active(rx)) {
		struct tbv_peer *peer;
		bool stale = false;

		/* Reject old PREPAREs even after QP number/PSN reuse. */
		mutex_lock(&state->lock);
		list_for_each_entry(peer, &state->peers, node)
			if (peer->remote_host_known &&
			    !memcmp(peer->remote_host_id, m->src_host, 16) &&
			    m->token <= peer->dmabuf_rx_token)
				stale = true;
		mutex_unlock(&state->lock);
		if (stale)
			return -ESTALE;
	}
	memset(&rx->batch, 0, sizeof(rx->batch));
	for (i = 0; i < m->count; i++) {
		struct tbv_rail *rail = tbv_dmabuf_find_lane(state, m, i);

		if (!rail) {
			ret = -ENOTCONN;
			goto drop;
		}
		for (j = 0; j < i; j++)
			if (rx->batch.lanes[j].rail == rail)
				break;
		if (j != i) {
			tbv_rail_put(rail);
			ret = -EINVAL;
			goto drop;
		}
		rx->batch.lanes[rx->batch.count++].rail = rail;
	}
	tbv_perf_wire(m, false, "rx_paths", perf_start, 0, m->count);
	if (!tbv_dmabuf_rx_active(rx)) {
		perf_start = tbv_perf_clock();
		ret = tbv_ibdev_dmabuf_begin(state, rx->batch.lanes[0].rail,
					     &m->hdr, &rx->buffer);
		tbv_perf_wire(m, false, "rx_verbs_begin", perf_start, ret, 0);
		if (ret)
			goto drop;
		rx->state = TBV_DMABUF_RX_OPEN;
		mutex_lock(&state->lock);
		{
			struct tbv_peer *peer;

			list_for_each_entry(peer, &state->peers, node)
				if (peer->remote_host_known &&
				    !memcmp(peer->remote_host_id, m->src_host, 16))
					peer->dmabuf_rx_token = m->token;
		}
		mutex_unlock(&state->lock);
	}
	rx->request = *m;
	rx->deadline = jiffies + msecs_to_jiffies(30000);
	start = tbv_dmabuf_clock();
	ret = tbv_dmabuf_batch_alloc(&rx->batch, m, &rx->buffer, false);
	tbv_dmabuf_record(TBV_DMABUF_RX_BUILD, start);
	if (ret)
		goto fail;
	start = tbv_dmabuf_clock();
	perf_start = tbv_perf_clock();
	ret = tbv_dmabuf_batch_post(&rx->batch, false);
	tbv_dmabuf_record(TBV_DMABUF_RX_POST, start);
	tbv_perf_wire(m, false, "batch_post", perf_start, ret, 0);
	if (ret)
		goto fail;
	rx->state = TBV_DMABUF_RX_ARMED;
	mod_delayed_work(system_unbound_wq, &dmabuf_engine.rx_watchdog,
			 msecs_to_jiffies(10));
	return TBV_DMABUF_READY;
drop:
	tbv_dmabuf_batch_drop(&rx->batch, false);
	return ret;
fail:
	tbv_dmabuf_rx_finish(ret);
	return ret;
}

static void tbv_dmabuf_rx_watch(struct work_struct *work)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_engine.rx;

	mutex_lock(&dmabuf_engine.rx_lock);
	if (tbv_dmabuf_rx_active(rx)) {
		if (!rx->buffer.alive(rx->buffer.context))
			tbv_dmabuf_rx_finish(-ECANCELED);
		else if (time_after_eq(jiffies, rx->deadline))
			tbv_dmabuf_rx_finish(-ETIMEDOUT);
		else if (rx->state == TBV_DMABUF_RX_ARMED) {
			tbv_dmabuf_rx_reap();
			if (rx->state == TBV_DMABUF_RX_ARMED)
				tbv_dmabuf_batch_kick(&rx->batch, false);
		}
		if (tbv_dmabuf_rx_active(rx))
			mod_delayed_work(system_unbound_wq, &dmabuf_engine.rx_watchdog,
					 msecs_to_jiffies(10));
	}
	mutex_unlock(&dmabuf_engine.rx_lock);
}

int tbv_dmabuf_control_request(struct tbv_peer *peer, struct tbv_control *ctrl,
			       struct tbv_dmabuf_wire *m)
{
	u64 start;
	u64 service;

	if (m->op == TBV_DMABUF_PING) {
		m->status = 0;
		return 0;
	}
	start = tbv_perf_clock();
	mutex_lock(&dmabuf_engine.rx_lock);
	tbv_perf_wire(m, false, "control_rx_lock", start, 0, m->op);
	service = tbv_perf_clock();
	if (m->op == TBV_DMABUF_PREPARE) {
		m->status = tbv_dmabuf_rx_prepare(peer->state, m);
		if (ctrl && m->status == TBV_DMABUF_READY) {
			/* Retried PREPAREs retain the existing tracker. Its RX ref
			 * survives rail release and disconnect on a different link. */
			if (ctrl && m->bytes && !dmabuf_engine.rx.control) {
				tbv_control_get(ctrl);
				dmabuf_engine.rx.control = ctrl;
			}
			/* Empty ownership barriers have no DMA callback. Complete
			 * them in the PREPARE reply, without an extra DONE packet,
			 * with the same QP/CQ checks as a payload WR. */
			if (!atomic_read(&dmabuf_engine.rx.batch.pending))
				m->status = tbv_dmabuf_rx_reap();
		}
	} else if (m->op == TBV_DMABUF_COMPLETE ||
		   !tbv_dmabuf_same(&dmabuf_engine.rx.request, m, true)) {
		m->status = -ENOENT;
	} else if (m->op == TBV_DMABUF_ABORT) {
		if (tbv_dmabuf_rx_active(&dmabuf_engine.rx))
			tbv_dmabuf_rx_finish(-ECANCELED);
		m->status = -ECANCELED;
	} else if (dmabuf_engine.rx.state == TBV_DMABUF_RX_CACHED) {
		m->status = dmabuf_engine.rx.status ?: TBV_DMABUF_DONE;
	} else {
		m->status = tbv_dmabuf_rx_reap();
	}
	mutex_unlock(&dmabuf_engine.rx_lock);
	tbv_perf_wire(m, false, "control_service", service, m->status, m->op);
	return m->status;
}

int tbv_dmabuf_handle_packet(struct tbv_state *state,
			     struct tb_xdomain *source, const void *buf, size_t size)
{
	struct tbv_dmabuf_wire m;
	struct tbv_peer *peer, *found = NULL;
	struct tb_xdomain *xd = NULL;
	u8 response[TBV_DMABUF_WIRE_SIZE];
	struct tbv_control *ctrl = NULL;
	u64 perf_start = tbv_perf_clock();
	int ret;

	/* Consume DONE before core matching, including malformed, late, or
	 * disabled-mode notifications. Only fully validated replies wake TX. */
	if (tbv_dmabuf_wire_notification(buf, size)) {
		if (dmabuf_verbs && !tbv_dmabuf_wire_parse(buf, size, &m) &&
		    m.op == (TBV_DMABUF_COMPLETE | TBV_DMABUF_REPLY))
			tbv_control_handle_done(state, source, &m);
		return 1;
	}
	if (!dmabuf_verbs)
		return 0;
	ret = tbv_dmabuf_wire_parse(buf, size, &m);
	if (ret) {
		if (size == TBV_DMABUF_WIRE_SIZE)
			pr_debug("control parse failed: %d\n", ret);
		return 0;
	}
	if (m.op & TBV_DMABUF_REPLY)
		return 0; /* tb_xdomain_request must see its response. */
	if (m.status || memcmp(m.dst_host, state->native_host_id, 16)) {
		pr_debug("control target mismatch token=%llu target=%pUb local=%pUb\n",
			 m.token, m.dst_host, state->native_host_id);
		return 1;
	}
	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		if ((source && peer->xd != source) ||
		    peer->backend != TBV_BACKEND_NATIVE ||
		    !peer->remote_host_known || !peer->xd->remote_uuid ||
		    memcmp(peer->remote_host_id, m.src_host, 16) ||
		    memcmp(peer->xd->remote_uuid, m.src_domain, 16))
			continue;
		xd = tb_xdomain_get(peer->xd);
		refcount_inc(&peer->refcnt);
		found = peer;
		break;
	}
	mutex_unlock(&state->lock);
	if (!xd) {
		pr_debug("control source mismatch token=%llu host=%pUb domain=%pUb\n",
			 m.token, m.src_host, m.src_domain);
		return 1;
	}
	tbv_perf_wire(&m, false, "control_dispatch", perf_start, 0, m.op);
	if (tbv_control_xdomain_required(found)) {
		ret = tbv_control_setup(found);
		if (!ret)
			ctrl = READ_ONCE(found->dmabuf_control);
		else
			m.status = ret;
	}
	if (!m.status)
		tbv_dmabuf_control_request(found, ctrl, &m);
	pr_debug("control reply op=%u token=%llu status=%d\n", m.op, m.token, m.status);
	m.op |= TBV_DMABUF_REPLY;
	perf_start = tbv_perf_clock();
	ret = tbv_dmabuf_wire_build(response, sizeof(response), &m, xd->route);
	if (ret >= 0)
		ret = tb_xdomain_response(xd, response, sizeof(response), TB_CFG_PKG_XDOMAIN_RESP);
	tbv_perf_wire(&m, false, "control_response", perf_start, ret, m.op);
	tb_xdomain_put(xd);
	tbv_peer_put(state, found);
	return 1;
}

static int tbv_dmabuf_exchange(struct tbv_peer *peer, struct tbv_ctrl_pending *pending,
			       const struct tbv_dmabuf_wire *m, u32 timeout_ms)
{
	u64 start;
	u64 perf_start = tbv_perf_clock();
	int ret;

	start = tbv_dmabuf_clock();
	if (pending) {
		ret = tbv_control_exchange(pending, m, timeout_ms);
	} else {
		ret = tbv_control_xdomain_exchange(peer, m, timeout_ms);
	}
	if (m->op == TBV_DMABUF_PREPARE || m->op == TBV_DMABUF_STATUS)
		tbv_dmabuf_record(m->op == TBV_DMABUF_PREPARE ?
			TBV_DMABUF_PREPARE_TIME : TBV_DMABUF_STATUS_TIME, start);
	if (ret < 0) {
		pr_debug("control exchange op=%u token=%llu transport=%d\n",
			 m->op, m->token, ret);
	}
	tbv_perf_wire(m, true, "control_exchange", perf_start, ret, m->op);
	return ret;
}

static int tbv_dmabuf_tx_paths(struct tbv_dmabuf_job *job,
			       struct tbv_dmabuf_wire *m,
			       struct tbv_dmabuf_batch *batch)
{
	struct tbv_state *state = job->state;
	struct tbv_peer *peer;

	mutex_lock(&state->lock);
	list_for_each_entry(peer, &state->peers, node) {
		struct tbv_rail *rail;

		if (!peer->remote_host_known ||
		    memcmp(peer->remote_host_id, job->route->peer->remote_host_id, 16))
			continue;
		list_for_each_entry(rail, &peer->rails, node) {
			u32 n = batch->count;

			if (n == TBV_DMABUF_MAX_RAILS || !tbv_dmabuf_rail_ready(rail) ||
			    !peer->xd->local_uuid)
				continue;
			refcount_inc(&rail->refcnt);
			batch->lanes[batch->count++].rail = rail;
			memcpy(m->lanes[n].domain, peer->xd->local_uuid, 16);
			m->lanes[n].path = rail->path.local_transmit_path;
		}
	}
	m->count = batch->count;
	mutex_unlock(&state->lock);
	return batch->count ? 0 : -ENOTCONN;
}

static int tbv_dmabuf_tx(struct tbv_dmabuf_job *job)
{
	struct tbv_dmabuf_wire m = {};
	struct tbv_dmabuf_batch batch = {};
	struct tbv_peer *peer = job->route->peer;
	struct tbv_ctrl_pending *pending = NULL;
	bool fast = tbv_control_required(peer);
	u32 offset = 0;
	int ret = 0;

	m.token = atomic64_inc_return(&dmabuf_engine.token);
	if (!m.token)
		return -EOVERFLOW;
	m.hdr = job->hdr;
	if (job->perf_queued_ns) {
		job->perf = tbv_perf_wire_id(&m, true);
		job->perf.wr_id = job->buffer.wr_id;
		job->perf.bytes = job->hdr.length;
	}
	m.hdr.flags |= TBV_NATIVE_DATA_F_RAW_STREAM;
	memcpy(m.src_host, job->state->native_host_id, 16);
	memcpy(m.dst_host, peer->remote_host_id, 16);
	memcpy(m.src_domain, peer->xd->local_uuid, 16);
	do {
		u64 start;
		u64 perf_start, batch_start = tbv_perf_clock();
		unsigned long deadline = jiffies + job->timeout * (job->retries + 1);
		u32 rnr = 0, failures = 0;
		bool remote_armed = false;

		memset(m.lanes, 0, sizeof(m.lanes));
		perf_start = tbv_perf_clock();
		mutex_lock(&dmabuf_engine.tx_lock);
		dmabuf_engine.active_context = job->buffer.context;
		dmabuf_engine.active_batch = &batch;
		ret = tbv_dmabuf_tx_paths(job, &m, &batch);
		tbv_perf_span(&job->perf, "tx_paths_lock", perf_start, ret, m.count);
		if (ret) {
			mutex_unlock(&dmabuf_engine.tx_lock);
			goto drop;
		}
		m.offset = offset;
		m.bytes = min_t(u32, job->hdr.length - offset,
			m.count * TBV_DMABUF_BATCH_FRAMES * TBV_DMABUF_FRAME_BYTES);
		start = tbv_dmabuf_clock();
		ret = tbv_dmabuf_batch_alloc(&batch, &m, &job->buffer, true);
		tbv_dmabuf_record(TBV_DMABUF_TX_BUILD, start);
		mutex_unlock(&dmabuf_engine.tx_lock);
		if (ret)
			goto drop;
		m.op = TBV_DMABUF_PREPARE;
		if (fast) {
			pending = tbv_control_open(peer, &m);
			if (IS_ERR(pending)) {
				ret = PTR_ERR(pending);
				pending = NULL;
				goto drop;
			}
		}
		for (;;) {
			if (!job->buffer.alive(job->buffer.context)) {
				ret = -ECANCELED;
				break;
			}
			ret = tbv_dmabuf_exchange(peer, pending, &m,
				clamp_t(u32, jiffies_to_msecs(job->timeout), 10, 1000));
			if (ret == TBV_DMABUF_READY) {
				remote_armed = true;
				break;
			}
			if (ret == TBV_DMABUF_DONE)
				break;
			if (ret == TBV_DMABUF_RNR) {
				if (job->rnr_retries != U8_MAX && rnr++ >= job->rnr_retries) {
					ret = -EAGAIN;
					break;
				}
				deadline = jiffies + job->timeout * (job->retries + 1);
			} else if (ret != TBV_DMABUF_BUSY &&
				   (ret != -ETIMEDOUT || failures++ >= job->retries)) {
				break;
			}
			if (time_after_eq(jiffies, deadline)) {
				ret = -ETIMEDOUT;
				break;
			}
			start = tbv_dmabuf_clock();
			perf_start = tbv_perf_clock();
			usleep_range(1000, 2000);
			tbv_dmabuf_record(ret == TBV_DMABUF_RNR ? TBV_DMABUF_RNR_WAIT :
				TBV_DMABUF_PREPARE_WAIT, start);
			tbv_perf_wire(&m, true, "prepare_retry_sleep", perf_start, ret, rnr);
		}
		if (ret == TBV_DMABUF_DONE) {
			tbv_control_close(pending);
			pending = NULL;
			mutex_lock(&dmabuf_engine.tx_lock);
			dmabuf_engine.active_context = NULL;
			dmabuf_engine.active_batch = NULL;
			tbv_dmabuf_batch_drop(&batch, false);
			mutex_unlock(&dmabuf_engine.tx_lock);
			tbv_perf_wire(&m, true, "batch", batch_start, 0, 0);
			offset += m.bytes;
			continue;
		}
		if (ret)
			goto abort;
		dma_wmb();
		start = tbv_dmabuf_clock();
		perf_start = tbv_perf_clock();
		mutex_lock(&dmabuf_engine.tx_lock);
		ret = job->buffer.alive(job->buffer.context) ?
			tbv_dmabuf_batch_post(&batch, true) : -ECANCELED;
		mutex_unlock(&dmabuf_engine.tx_lock);
		tbv_dmabuf_record(TBV_DMABUF_TX_POST, start);
		tbv_perf_wire(&m, true, "batch_post", perf_start, ret, 0);
		if (!ret) {
			start = tbv_dmabuf_clock();
			perf_start = tbv_perf_clock();
			ret = tbv_dmabuf_batch_wait(&batch, &job->buffer, deadline);
			tbv_dmabuf_record(TBV_DMABUF_TX_WAIT, start);
			tbv_perf_wire(&m, true, "tx_dma_wait", perf_start, ret, 0);
		}
		if (ret)
			goto abort;
		m.op = TBV_DMABUF_STATUS;
		start = tbv_dmabuf_clock();
		for (;;) {
			if (!job->buffer.alive(job->buffer.context)) {
				ret = -ECANCELED;
				break;
			}
			if (pending) {
				/* DONE is registered before PREPARE, so a notification
				 * arriving before local TX completion is retained. Only
				 * probe cached status after a missed 10 ms notification. */
				ret = tbv_control_wait_done(pending, 10);
				if (ret == TBV_DMABUF_BUSY) {
					tbv_perf_wire_mark(&m, true, "status_fallback", ret, 0);
					ret = tbv_dmabuf_exchange(peer, pending, &m,
						clamp_t(u32, jiffies_to_msecs(job->timeout), 10, 1000));
				}
			} else {
				ret = tbv_dmabuf_exchange(peer, NULL, &m,
					clamp_t(u32, jiffies_to_msecs(job->timeout), 10, 1000));
			}
			if (ret == TBV_DMABUF_DONE) {
				ret = 0;
				break;
			}
			if (ret != TBV_DMABUF_BUSY && ret != -ETIMEDOUT)
				break;
			if (time_after_eq(jiffies, deadline)) {
				ret = -ETIMEDOUT;
				break;
			}
			if (!pending) {
				u64 sleep_start = tbv_dmabuf_clock();
				u64 perf_sleep = tbv_perf_clock();

				usleep_range(100, 200);
				tbv_dmabuf_record(TBV_DMABUF_STATUS_WAIT, sleep_start);
				tbv_perf_wire(&m, true, "status_retry_sleep", perf_sleep, ret, 0);
			}
		}
		if (pending)
			tbv_dmabuf_record(TBV_DMABUF_DONE_WAIT, start);
abort:
		if (ret && (remote_armed || ret == -ETIMEDOUT || ret == -EPROTO)) {
			m.op = TBV_DMABUF_ABORT;
			tbv_dmabuf_exchange(peer, pending, &m, 1000);
		}
drop:
		tbv_control_close(pending);
		pending = NULL;
		mutex_lock(&dmabuf_engine.tx_lock);
		dmabuf_engine.active_context = NULL;
		dmabuf_engine.active_batch = NULL;
		tbv_dmabuf_batch_drop(&batch, !!ret);
		mutex_unlock(&dmabuf_engine.tx_lock);
		tbv_perf_wire(&m, true, "batch", batch_start, ret, 0);
		if (ret)
			return ret;
		offset += m.bytes;
	} while (offset < job->hdr.length);
	return 0;
}

static void tbv_dmabuf_tx_work(struct work_struct *work)
{
	struct tbv_dmabuf_job *job = container_of(work, struct tbv_dmabuf_job, work);
	u64 start = tbv_dmabuf_clock();
	u64 finish;
	u64 perf_start = tbv_perf_clock(), perf_end, perf_finish;
	int ret;

	tbv_dmabuf_record(TBV_DMABUF_QUEUE, job->queued_ns);
	ret = job->buffer.alive(job->buffer.context) ? tbv_dmabuf_tx(job) : -ECANCELED;
	perf_end = tbv_perf_clock();
	tbv_perf_interval(&job->perf, "tx_queue", job->perf_queued_ns, perf_start, 0, 0);
	tbv_perf_interval(&job->perf, "tx_job", perf_start, perf_end, ret, 0);
	tbv_dmabuf_record(TBV_DMABUF_JOB, start);
	finish = tbv_dmabuf_clock();
	perf_finish = tbv_perf_clock();
	job->buffer.finish(job->buffer.context, ret);
	tbv_perf_span(&job->perf, "tx_finish", perf_finish, ret, 0);
	tbv_perf_span(&job->perf, "tx_wr", job->perf_queued_ns, ret, 0);
	tbv_dmabuf_record(TBV_DMABUF_TX_FINISH, finish);
	tbv_rail_put(job->route);
	kfree(job);
}

int tbv_dmabuf_submit(struct tbv_state *state, struct tbv_rail *rail,
		       const struct tbv_native_data_header *hdr,
		       const struct tbv_dmabuf_buffer *buffer, u8 retries,
		       u8 rnr_retries, unsigned long timeout)
{
	struct tbv_dmabuf_job *job;

	if (!dmabuf_engine.tx_wq || !rail || !tbv_dmabuf_rail_ready(rail))
		return -ENOTCONN;
	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;
	INIT_WORK(&job->work, tbv_dmabuf_tx_work);
	job->state = state;
	job->route = rail;
	job->hdr = *hdr;
	job->buffer = *buffer;
	job->retries = retries;
	job->rnr_retries = rnr_retries;
	job->timeout = timeout ?: msecs_to_jiffies(5000);
	job->queued_ns = tbv_dmabuf_clock();
	job->perf_queued_ns = tbv_perf_clock();
	if (job->perf_queued_ns) {
		job->perf.src_qp = hdr->src_qp;
		job->perf.wr_id = buffer->wr_id;
		job->perf.dst_qp = hdr->dest_qp;
		job->perf.psn = hdr->psn;
		job->perf.bytes = hdr->length;
		job->perf.tx = true;
		job->perf.opcode = hdr->opcode;
	}
	refcount_inc(&rail->refcnt);
	queue_work(dmabuf_engine.tx_wq, &job->work);
	return 0;
}

void tbv_dmabuf_disconnect(struct tbv_rail *rail)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_engine.rx;
	u32 i;

	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_engine.rx_lock);
	if (tbv_dmabuf_rx_active(rx)) {
		for (i = 0; i < rx->batch.count; i++)
			if (rx->batch.lanes[i].rail == rail) {
				tbv_dmabuf_rx_finish(-ENOTCONN);
				break;
			}
	}
	mutex_unlock(&dmabuf_engine.rx_lock);
}

int tbv_dmabuf_start(struct tbv_state *state)
{
	if (!dmabuf_verbs)
		return 0;
	/* Descriptor draining depends on Thunderbolt's ordinary ring workers;
	 * neither this queue nor the verbs error queue is a reclaim worker. */
	dmabuf_engine.tx_wq = alloc_ordered_workqueue("tbv_dmabuf_tx", 0);
	if (!dmabuf_engine.tx_wq)
		return -ENOMEM;
	dmabuf_engine.post_wq = alloc_workqueue("tbv_dmabuf_post", WQ_UNBOUND | WQ_HIGHPRI,
					TBV_DMABUF_MAX_RAILS);
	if (!dmabuf_engine.post_wq) {
		destroy_workqueue(dmabuf_engine.tx_wq);
		dmabuf_engine.tx_wq = NULL;
		return -ENOMEM;
	}
	memset(&dmabuf_engine.rx, 0, sizeof(dmabuf_engine.rx));
	INIT_DELAYED_WORK(&dmabuf_engine.rx_watchdog, tbv_dmabuf_rx_watch);
	INIT_WORK(&dmabuf_engine.rx_complete_work, tbv_dmabuf_rx_completed);
	return 0;
}

void tbv_dmabuf_stop(void)
{
	if (!dmabuf_engine.tx_wq)
		return;
	cancel_delayed_work_sync(&dmabuf_engine.rx_watchdog);
	mutex_lock(&dmabuf_engine.rx_lock);
	if (tbv_dmabuf_rx_active(&dmabuf_engine.rx))
		tbv_dmabuf_rx_finish(-ECANCELED);
	mutex_unlock(&dmabuf_engine.rx_lock);
	cancel_work_sync(&dmabuf_engine.rx_complete_work);
	destroy_workqueue(dmabuf_engine.tx_wq);
	dmabuf_engine.tx_wq = NULL;
	destroy_workqueue(dmabuf_engine.post_wq);
	dmabuf_engine.post_wq = NULL;
}
