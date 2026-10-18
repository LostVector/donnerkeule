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
	TBV_DMABUF_PHASE_COUNT,
};

static const char * const dmabuf_phase_names[] = {
	"queue", "job", "tx_build", "prepare", "tx_post", "tx_wait", "status",
	"tx_release", "tx_finish", "rx_build", "rx_post", "rx_reap", "rx_release",
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
	struct tbv_path *path;
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
};

struct tbv_dmabuf_batch {
	struct tbv_dmabuf_lane lanes[TBV_DMABUF_MAX_RAILS];
	atomic_t pending;
	atomic_t error;
	wait_queue_head_t wait;
	u32 count;
	bool submitted;
	bool tx;
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
};

struct tbv_dmabuf_rx {
	struct tbv_dmabuf_wire request;
	struct tbv_dmabuf_buffer buffer;
	struct tbv_dmabuf_batch batch;
	unsigned long deadline;
	u32 next_offset;
	int status;
	bool active;
	bool armed;
	bool cached;
};

static struct workqueue_struct *dmabuf_tx_wq;
static DEFINE_MUTEX(dmabuf_rx_lock);
static DEFINE_MUTEX(dmabuf_ring_lock);
static DEFINE_MUTEX(dmabuf_tx_lock);
static void *dmabuf_active_context;
static struct tbv_dmabuf_batch *dmabuf_active_batch;
static struct tbv_dmabuf_rx dmabuf_rx;
static struct delayed_work dmabuf_rx_watchdog;
static atomic64_t dmabuf_token = ATOMIC64_INIT(0);

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
	return ret;
}

/* Raw frames have no operation identifier. A failed batch quarantines both
 * directions until the physical connection/module is recreated. Draining
 * precedes every release of frame metadata, mappings and verbs references. */
static void tbv_dmabuf_stop_rail(struct tbv_rail *rail)
{
	mutex_lock(&dmabuf_ring_lock);
	if (!READ_ONCE(rail->gpu_direct_used)) {
		WRITE_ONCE(rail->gpu_direct_used, true);
		tb_ring_stop(rail->path.tx_ring);
		tb_ring_stop(rail->path.rx_ring);
	}
	mutex_unlock(&dmabuf_ring_lock);
}

static void tbv_dmabuf_batch_drop(struct tbv_dmabuf_batch *batch, bool abort)
{
	u64 start = tbv_dmabuf_clock();
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
}

void tbv_dmabuf_cancel(void *context)
{
	u32 i;

	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_tx_lock);
	if (dmabuf_active_context == context && dmabuf_active_batch)
		for (i = 0; i < dmabuf_active_batch->count; i++)
			tbv_dmabuf_stop_rail(dmabuf_active_batch->lanes[i].rail);
	mutex_unlock(&dmabuf_tx_lock);
}

static bool tbv_dmabuf_rail_ready(struct tbv_rail *rail)
{
	return rail->peer->backend == TBV_BACKEND_NATIVE &&
		!READ_ONCE(rail->removing) && !READ_ONCE(rail->gpu_direct_used) &&
		rail->peer->remote_host_known && tbv_rail_data_ready(rail);
}

static void tbv_dmabuf_complete(struct tb_ring *ring, struct ring_frame *frame,
				bool canceled)
{
	struct tbv_dmabuf_frame *f = container_of(frame, struct tbv_dmabuf_frame,
						  frame);
	struct tbv_state *state = f->path->rail->peer->state;
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
	if (canceled) {
		atomic64_inc(ring->is_tx ? &state->data_tx_canceled :
					 &state->data_rx_canceled);
	} else if (ring->is_tx) {
		atomic64_inc(&state->data_tx_completed);
		atomic64_inc(&f->path->data_tx_completed);
		atomic64_add(f->bytes, &f->path->tx_bytes);
	} else {
		atomic64_inc(&state->data_rx_completed);
		atomic64_inc(&f->path->data_rx_completed);
		atomic64_add(f->bytes, &f->path->rx_bytes);
		if (error)
			atomic64_inc(&state->data_rx_bad_frame);
	}
	if (atomic_dec_and_test(&f->batch->pending) || error)
		wake_up_all(&f->batch->wait);
}

static int tbv_dmabuf_batch_alloc(struct tbv_dmabuf_batch *batch,
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

		lane->first = DIV_ROUND_UP((u64)frames * i, batch->count);
		lane->count = end - lane->first;
		lane->dev = tb_ring_dma_device(ring);
		if (lane->count >= ring->size || !tbv_dma_device_ready(lane->dev))
			return -ENODEV;
		if (!lane->count)
			continue;
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
		for (n = 0; n < lane->count; n++) {
			struct tbv_dmabuf_frame *f = &lane->frames[n];
			u32 offset = (lane->first + n) * TBV_DMABUF_FRAME_BYTES;
			int ret;

			INIT_LIST_HEAD(&f->frame.list);
			f->batch = batch;
			f->path = &lane->rail->path;
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
	}
	return 0;
}

static int tbv_dmabuf_batch_post(struct tbv_dmabuf_batch *batch, bool tx)
{
	u32 i;

	for (i = 0; i < batch->count; i++) {
		struct tbv_dmabuf_lane *lane = &batch->lanes[i];
		struct tb_ring *ring = tx ? lane->rail->path.tx_ring :
					   lane->rail->path.rx_ring;
		u32 n;

		if (!tbv_dmabuf_rail_ready(lane->rail))
			return -ENOTCONN;
		for (n = 0; n < lane->count; n++) {
			int ret;

			batch->submitted = true;
			ret = tx ? tb_ring_tx(ring, &lane->frames[n].frame) :
				   tb_ring_rx(ring, &lane->frames[n].frame);
			if (ret)
				return ret;
			if (tx) {
				atomic64_inc(&lane->rail->peer->state->data_tx_posted);
				atomic64_inc(&lane->rail->path.data_tx_posted);
			}
		}
	}
	return 0;
}

static void tbv_dmabuf_batch_kick(struct tbv_dmabuf_batch *batch, bool tx)
{
	u32 i;

	for (i = 0; i < batch->count; i++) {
		struct tbv_rail *rail = batch->lanes[i].rail;
		struct tb_ring *ring = tx ? rail->path.tx_ring : rail->path.rx_ring;

		mutex_lock(&dmabuf_ring_lock);
		if (!READ_ONCE(rail->gpu_direct_used))
			schedule_work(&ring->work);
		mutex_unlock(&dmabuf_ring_lock);
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
		wait_event_timeout(batch->wait, !atomic_read(&batch->pending) ||
			atomic_read(&batch->error), msecs_to_jiffies(10));
		tbv_dmabuf_batch_kick(batch, true);
	}
	return atomic_read(&batch->error);
}

static bool tbv_dmabuf_same(const struct tbv_dmabuf_wire *a,
			    const struct tbv_dmabuf_wire *b, bool batch)
{
	return a->token == b->token && !memcmp(a->src_host, b->src_host, 16) &&
		!memcmp(a->dst_host, b->dst_host, 16) &&
		!memcmp(&a->hdr, &b->hdr, sizeof(a->hdr)) &&
		a->count == b->count &&
		!memcmp(a->lanes, b->lanes, sizeof(a->lanes)) &&
		(!batch || (a->offset == b->offset && a->bytes == b->bytes));
}

static void tbv_dmabuf_rx_finish(int status)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_rx;

	tbv_dmabuf_batch_drop(&rx->batch, !!status);
	rx->armed = false;
	if (rx->active)
		status = rx->buffer.finish(rx->buffer.context, status);
	memset(&rx->buffer, 0, sizeof(rx->buffer));
	rx->active = false;
	rx->cached = true;
	rx->status = status;
}

void tbv_dmabuf_cancel_recv(void *owner)
{
	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_rx_lock);
	if (dmabuf_rx.active && dmabuf_rx.buffer.owner == owner)
		tbv_dmabuf_rx_finish(-ECANCELED);
	mutex_unlock(&dmabuf_rx_lock);
}

static int tbv_dmabuf_rx_reap_impl(void)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_rx;
	u32 i;
	int ret = atomic_read(&rx->batch.error);

	if (!rx->armed)
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
		rx->armed = false;
	}
	return TBV_DMABUF_DONE;
fail:
	tbv_dmabuf_rx_finish(ret);
	return ret;
}

static int tbv_dmabuf_rx_reap(void)
{
	u64 start = tbv_dmabuf_clock();
	int ret = tbv_dmabuf_rx_reap_impl();

	tbv_dmabuf_record(TBV_DMABUF_RX_REAP, start);
	return ret;
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
	struct tbv_dmabuf_rx *rx = &dmabuf_rx;
	u64 start;
	u32 i, j;
	int ret;

	if (rx->cached && tbv_dmabuf_same(&rx->request, m, true))
		return rx->status ?: TBV_DMABUF_DONE;
	if (rx->active) {
		if (!tbv_dmabuf_same(&rx->request, m, false))
			return TBV_DMABUF_BUSY;
		if (rx->armed)
			return tbv_dmabuf_same(&rx->request, m, true) ?
				TBV_DMABUF_READY : -EINVAL;
		if (m->offset == rx->request.offset && m->bytes == rx->request.bytes)
			return TBV_DMABUF_DONE;
		if (m->offset != rx->next_offset)
			return -EINVAL;
	} else if (m->offset) {
		return -EINVAL;
	}
	if (!rx->active) {
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
	if (!rx->active) {
		ret = tbv_ibdev_dmabuf_begin(state, rx->batch.lanes[0].rail,
					     &m->hdr, &rx->buffer);
		if (ret)
			goto drop;
		rx->active = true;
		rx->cached = false;
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
	ret = tbv_dmabuf_batch_post(&rx->batch, false);
	tbv_dmabuf_record(TBV_DMABUF_RX_POST, start);
	if (ret)
		goto fail;
	rx->armed = true;
	mod_delayed_work(system_unbound_wq, &dmabuf_rx_watchdog,
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
	struct tbv_dmabuf_rx *rx = &dmabuf_rx;

	mutex_lock(&dmabuf_rx_lock);
	if (rx->active) {
		if (!rx->buffer.alive(rx->buffer.context))
			tbv_dmabuf_rx_finish(-ECANCELED);
		else if (time_after_eq(jiffies, rx->deadline))
			tbv_dmabuf_rx_finish(-ETIMEDOUT);
		else if (rx->armed) {
			tbv_dmabuf_rx_reap();
			if (rx->armed)
				tbv_dmabuf_batch_kick(&rx->batch, false);
		}
		if (rx->active)
			mod_delayed_work(system_unbound_wq, &dmabuf_rx_watchdog,
					 msecs_to_jiffies(10));
	}
	mutex_unlock(&dmabuf_rx_lock);
}

int tbv_dmabuf_handle_packet(struct tbv_state *state,
			     struct tb_xdomain *source, const void *buf, size_t size)
{
	struct tbv_dmabuf_wire m;
	struct tbv_peer *peer;
	struct tb_xdomain *xd = NULL;
	u8 response[TBV_DMABUF_WIRE_SIZE];
	int ret;

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
		break;
	}
	mutex_unlock(&state->lock);
	if (!xd) {
		pr_debug("control source mismatch token=%llu host=%pUb domain=%pUb\n",
			 m.token, m.src_host, m.src_domain);
		return 1;
	}
	mutex_lock(&dmabuf_rx_lock);
	if (m.op == TBV_DMABUF_PREPARE) {
		m.status = tbv_dmabuf_rx_prepare(state, &m);
	} else if (!tbv_dmabuf_same(&dmabuf_rx.request, &m, true)) {
		m.status = -ENOENT;
	} else if (m.op == TBV_DMABUF_ABORT) {
		if (dmabuf_rx.active)
			tbv_dmabuf_rx_finish(-ECANCELED);
		m.status = -ECANCELED;
	} else if (dmabuf_rx.cached) {
		m.status = dmabuf_rx.status ?: TBV_DMABUF_DONE;
	} else {
		m.status = tbv_dmabuf_rx_reap();
	}
	mutex_unlock(&dmabuf_rx_lock);
	pr_debug("control reply op=%u token=%llu status=%d\n", m.op, m.token, m.status);
	m.op |= TBV_DMABUF_REPLY;
	ret = tbv_dmabuf_wire_build(response, sizeof(response), &m, xd->route);
	if (ret >= 0)
		tb_xdomain_response(xd, response, sizeof(response), TB_CFG_PKG_XDOMAIN_RESP);
	tb_xdomain_put(xd);
	return 1;
}

static int tbv_dmabuf_exchange(struct tbv_peer *peer,
			       const struct tbv_dmabuf_wire *m, u32 timeout_ms)
{
	struct tbv_dmabuf_wire reply;
	u8 request[TBV_DMABUF_WIRE_SIZE], response[TBV_DMABUF_WIRE_SIZE] = {};
	u64 start;
	int ret;

	ret = tbv_dmabuf_wire_build(request, sizeof(request), m, peer->xd->route);
	if (ret < 0)
		return ret;
	start = tbv_dmabuf_clock();
	mutex_lock(&peer->control_lock);
	ret = tb_xdomain_request(peer->xd, request, sizeof(request),
		TB_CFG_PKG_XDOMAIN_REQ, response, sizeof(response),
		TB_CFG_PKG_XDOMAIN_RESP, timeout_ms);
	mutex_unlock(&peer->control_lock);
	if (m->op == TBV_DMABUF_PREPARE || m->op == TBV_DMABUF_STATUS)
		tbv_dmabuf_record(m->op == TBV_DMABUF_PREPARE ?
			TBV_DMABUF_PREPARE_TIME : TBV_DMABUF_STATUS_TIME, start);
	if (ret) {
		pr_debug("control exchange op=%u token=%llu transport=%d\n",
			 m->op, m->token, ret);
		return ret;
	}
	ret = tbv_dmabuf_wire_parse(response, sizeof(response), &reply);
	if (ret || reply.op != (m->op | TBV_DMABUF_REPLY) ||
	    !tbv_dmabuf_same(m, &reply, true) ||
	    memcmp(m->src_domain, reply.src_domain, 16))
		return -EPROTO;
	return reply.status;
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
	u32 offset = 0;
	int ret = 0;

	m.token = atomic64_inc_return(&dmabuf_token);
	if (!m.token)
		return -EOVERFLOW;
	m.hdr = job->hdr;
	m.hdr.flags |= TBV_NATIVE_DATA_F_RAW_STREAM;
	memcpy(m.src_host, job->state->native_host_id, 16);
	memcpy(m.dst_host, peer->remote_host_id, 16);
	memcpy(m.src_domain, peer->xd->local_uuid, 16);
	do {
		u64 start;
		unsigned long deadline = jiffies + job->timeout * (job->retries + 1);
		u32 rnr = 0, failures = 0;
		bool remote_armed = false;

		memset(m.lanes, 0, sizeof(m.lanes));
		mutex_lock(&dmabuf_tx_lock);
		dmabuf_active_context = job->buffer.context;
		dmabuf_active_batch = &batch;
		ret = tbv_dmabuf_tx_paths(job, &m, &batch);
		if (ret) {
			mutex_unlock(&dmabuf_tx_lock);
			goto drop;
		}
		m.offset = offset;
		m.bytes = min_t(u32, job->hdr.length - offset,
			m.count * TBV_DMABUF_BATCH_FRAMES * TBV_DMABUF_FRAME_BYTES);
		start = tbv_dmabuf_clock();
		ret = tbv_dmabuf_batch_alloc(&batch, &m, &job->buffer, true);
		tbv_dmabuf_record(TBV_DMABUF_TX_BUILD, start);
		mutex_unlock(&dmabuf_tx_lock);
		if (ret)
			goto drop;
		m.op = TBV_DMABUF_PREPARE;
		for (;;) {
			if (!job->buffer.alive(job->buffer.context)) {
				ret = -ECANCELED;
				break;
			}
			ret = tbv_dmabuf_exchange(peer, &m,
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
			usleep_range(1000, 2000);
		}
		if (ret == TBV_DMABUF_DONE) {
			mutex_lock(&dmabuf_tx_lock);
			dmabuf_active_context = NULL;
			dmabuf_active_batch = NULL;
			tbv_dmabuf_batch_drop(&batch, false);
			mutex_unlock(&dmabuf_tx_lock);
			offset += m.bytes;
			continue;
		}
		if (ret)
			goto abort;
		dma_wmb();
		start = tbv_dmabuf_clock();
		mutex_lock(&dmabuf_tx_lock);
		ret = job->buffer.alive(job->buffer.context) ?
			tbv_dmabuf_batch_post(&batch, true) : -ECANCELED;
		mutex_unlock(&dmabuf_tx_lock);
		tbv_dmabuf_record(TBV_DMABUF_TX_POST, start);
		if (!ret) {
			start = tbv_dmabuf_clock();
			ret = tbv_dmabuf_batch_wait(&batch, &job->buffer, deadline);
			tbv_dmabuf_record(TBV_DMABUF_TX_WAIT, start);
		}
		if (ret)
			goto abort;
		m.op = TBV_DMABUF_STATUS;
		for (;;) {
			if (!job->buffer.alive(job->buffer.context)) {
				ret = -ECANCELED;
				break;
			}
			ret = tbv_dmabuf_exchange(peer, &m,
				clamp_t(u32, jiffies_to_msecs(job->timeout), 10, 1000));
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
			usleep_range(100, 200);
		}
abort:
		if (ret && (remote_armed || ret == -ETIMEDOUT || ret == -EPROTO)) {
			m.op = TBV_DMABUF_ABORT;
			tbv_dmabuf_exchange(peer, &m, 1000);
		}
drop:
		mutex_lock(&dmabuf_tx_lock);
		dmabuf_active_context = NULL;
		dmabuf_active_batch = NULL;
		tbv_dmabuf_batch_drop(&batch, !!ret);
		mutex_unlock(&dmabuf_tx_lock);
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
	int ret;

	tbv_dmabuf_record(TBV_DMABUF_QUEUE, job->queued_ns);
	ret = job->buffer.alive(job->buffer.context) ? tbv_dmabuf_tx(job) : -ECANCELED;
	tbv_dmabuf_record(TBV_DMABUF_JOB, start);
	finish = tbv_dmabuf_clock();
	job->buffer.finish(job->buffer.context, ret);
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

	if (!dmabuf_tx_wq || !rail || !tbv_dmabuf_rail_ready(rail))
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
	refcount_inc(&rail->refcnt);
	queue_work(dmabuf_tx_wq, &job->work);
	return 0;
}

void tbv_dmabuf_disconnect(struct tbv_rail *rail)
{
	struct tbv_dmabuf_rx *rx = &dmabuf_rx;
	u32 i;

	if (!dmabuf_verbs)
		return;
	mutex_lock(&dmabuf_rx_lock);
	if (rx->active) {
		for (i = 0; i < rx->batch.count; i++)
			if (rx->batch.lanes[i].rail == rail) {
				tbv_dmabuf_rx_finish(-ENOTCONN);
				break;
			}
	}
	mutex_unlock(&dmabuf_rx_lock);
}

int tbv_dmabuf_start(struct tbv_state *state)
{
	if (!dmabuf_verbs)
		return 0;
	/* Descriptor draining depends on Thunderbolt's ordinary ring workers;
	 * neither this queue nor the verbs error queue is a reclaim worker. */
	dmabuf_tx_wq = alloc_ordered_workqueue("tbv_dmabuf_tx", 0);
	if (!dmabuf_tx_wq)
		return -ENOMEM;
	memset(&dmabuf_rx, 0, sizeof(dmabuf_rx));
	INIT_DELAYED_WORK(&dmabuf_rx_watchdog, tbv_dmabuf_rx_watch);
	return 0;
}

void tbv_dmabuf_stop(void)
{
	if (!dmabuf_tx_wq)
		return;
	cancel_delayed_work_sync(&dmabuf_rx_watchdog);
	mutex_lock(&dmabuf_rx_lock);
	if (dmabuf_rx.active)
		tbv_dmabuf_rx_finish(-ECANCELED);
	mutex_unlock(&dmabuf_rx_lock);
	destroy_workqueue(dmabuf_tx_wq);
	dmabuf_tx_wq = NULL;
}
