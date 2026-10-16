// SPDX-License-Identifier: GPL-2.0
/* Experimental, exclusive DMA-BUF message path over the native TBV rails.
 * No CPU mapping/copy of payload and no verbs emulation. Userspace provides
 * the rendezvous, GPU release/acquire and GPU-consumer lifetime contract.
 */
#define pr_fmt(fmt) "thunderbolt_ibverbs gpu_direct: " fmt

#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/thunderbolt.h>
#include <linux/uaccess.h>

#include "../include/tbv_gpu_direct.h"
#include "../proto/gpu_pool.h"
#include "tbv.h"
#include "perf.h"

static bool gpu_direct;
module_param(gpu_direct, bool, 0444);
MODULE_PARM_DESC(gpu_direct,
	"Exclusive experimental GPU DMA-BUF pools; requires native-only, register_verbs=0");

struct tbv_gpu_map {
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	dma_addr_t *frames;
	enum dma_data_direction direction;
};

struct tbv_gpu_batch {
	wait_queue_head_t wait;
	atomic_t pending;
	atomic_t error;
	u64 sequence;
	u64 bytes;
	bool active;
	bool waiting;
	struct tbv_perf_id perf;
};

struct tbv_gpu_frame {
	struct ring_frame frame;
	struct tbv_gpu_batch *batch;
	struct tbv_gpu_lane *lane;
};

struct tbv_gpu_lane {
	struct tbv_rail *rail;
	struct tbv_gpu_map tx;
	struct tbv_gpu_map rx;
	struct tbv_gpu_frame *tx_frames;
	struct tbv_gpu_frame *rx_frames;
	struct tbv_perf_id perf[2];
	atomic_t perf_pending[2];
	u32 perf_count[2];
	bool perf_active[2];
};

struct tbv_gpu_session {
	struct tbv_gpu_lane lanes[TBV_GPU_MAX_RAILS];
	struct dma_buf *tx;
	struct dma_buf *rx;
	struct tbv_gpu_batch tx_batch;
	struct tbv_gpu_batch rx_batch;
	u64 bytes;
	u32 count;
	bool imported;
	bool aborted;
	bool submitted;
	u64 perf_session;
};

/* Lock order: gpu_lock -> state->lock. Callbacks never take either lock.
 * WAIT drops gpu_lock; a disconnect can stop DMA and wake it immediately. */
static DEFINE_MUTEX(gpu_lock);
static struct tbv_state *gpu_state;
static struct tbv_gpu_session *gpu_session;
static atomic64_t gpu_work_kicks = ATOMIC64_INIT(0);
static atomic64_t gpu_wait_after_kick = ATOMIC64_INIT(0);
static atomic64_t gpu_perf_session = ATOMIC64_INIT(0);

bool tbv_gpu_direct_enabled(void)
{
	return gpu_direct;
}

u64 tbv_gpu_direct_work_kicks(void)
{
	return atomic64_read(&gpu_work_kicks);
}

u64 tbv_gpu_direct_wait_after_kick(void)
{
	return atomic64_read(&gpu_wait_after_kick);
}

static void tbv_gpu_unmap(struct tbv_gpu_map *map)
{
	kvfree(map->frames);
	if (map->sgt)
		dma_buf_unmap_attachment_unlocked(map->attach, map->sgt,
						 map->direction);
	if (map->attach)
		dma_buf_detach(map->attach->dmabuf, map->attach);
	memset(map, 0, sizeof(*map));
}

static int tbv_gpu_map(struct tbv_gpu_map *map, struct dma_buf *buf,
		       struct device *dev, u64 bytes,
		       enum dma_data_direction direction)
{
	struct scatterlist *sg;
	u64 covered = 0;
	u32 n = 0;
	int i;

	if (!tbv_dma_device_ready(dev))
		return -ENODEV;
	map->direction = direction;
	/* Static attachment pins backing storage, including dynamic exporters.
	 * Do not call dma_buf_pin(): that API is for dynamic importers only. */
	map->attach = dma_buf_attach(buf, dev);
	if (IS_ERR(map->attach)) {
		int ret = PTR_ERR(map->attach);

		map->attach = NULL;
		return ret;
	}
	map->sgt = dma_buf_map_attachment_unlocked(map->attach, direction);
	if (IS_ERR(map->sgt)) {
		int ret = PTR_ERR(map->sgt);

		map->sgt = NULL;
		return ret;
	}
	map->frames = kvcalloc(bytes / TBV_GPU_FRAME_BYTES,
			       sizeof(*map->frames), GFP_KERNEL);
	if (!map->frames)
		return -ENOMEM;
	for_each_sgtable_dma_sg(map->sgt, sg, i) {
		u64 length = min_t(u64, sg_dma_len(sg), bytes - covered);
		u64 off;

		if (!length || length % TBV_GPU_FRAME_BYTES)
			return -EINVAL;
		for (off = 0; off < length; off += TBV_GPU_FRAME_BYTES) {
			u64 address;

			if (tbv_gpu_segment_frame(sg_dma_address(sg),
						  sg_dma_len(sg), off, &address) ||
			    (dma_addr_t)address != address)
				return -EINVAL;
			map->frames[n++] = address;
		}
		covered += length;
		if (covered == bytes)
			return 0;
	}
	return -EINVAL;
}

static void tbv_gpu_drop_import(struct tbv_gpu_session *s)
{
	u32 i;

	for (i = s->count; i-- > 0;) {
		struct tbv_gpu_lane *lane = &s->lanes[i];

		tbv_gpu_unmap(&lane->rx);
		tbv_gpu_unmap(&lane->tx);
		kfree(lane->rx_frames);
		kfree(lane->tx_frames);
		if (lane->rail)
			tbv_rail_put(lane->rail);
		memset(lane, 0, sizeof(*lane));
	}
	if (s->rx)
		dma_buf_put(s->rx);
	if (s->tx)
		dma_buf_put(s->tx);
	s->rx = NULL;
	s->tx = NULL;
	s->count = 0;
	s->imported = false;
}

static void tbv_gpu_abort(struct tbv_gpu_session *s)
{
	u32 i;

	if (s->aborted)
		return;
	s->aborted = true;
	atomic_cmpxchg(&s->tx_batch.error, 0, -ECANCELED);
	atomic_cmpxchg(&s->rx_batch.error, 0, -ECANCELED);
	for (i = 0; i < s->count; i++) {
		struct tbv_rail *rail = s->lanes[i].rail;

		if (!s->submitted)
			continue;
		/* No reuse after close/failure: raw frames have no generation ID.
		 * Reload/reconnect both peers before another session. Keep the path
		 * state for normal tunnel teardown, but permanently reject imports. */
		WRITE_ONCE(rail->gpu_direct_used, true);
		tb_ring_stop(rail->path.tx_ring);
		tb_ring_stop(rail->path.rx_ring);
	}
	wake_up_all(&s->tx_batch.wait);
	wake_up_all(&s->rx_batch.wait);
	/* tb_ring_stop drains canceled callbacks before maps/frames are freed. */
	tbv_gpu_drop_import(s);
}

void tbv_gpu_direct_disconnect(struct tbv_rail *rail)
{
	u32 i;

	if (!gpu_direct)
		return;
	mutex_lock(&gpu_lock);
	if (gpu_session) {
		for (i = 0; i < gpu_session->count; i++) {
			if (gpu_session->lanes[i].rail == rail) {
				tbv_gpu_abort(gpu_session);
				break;
			}
		}
	}
	mutex_unlock(&gpu_lock);
}

static bool tbv_gpu_rail_ready(struct tbv_rail *rail)
{
	return rail->peer->backend == TBV_BACKEND_NATIVE &&
		!rail->removing && !rail->gpu_direct_used &&
		rail->peer->remote_host_known && tbv_rail_data_ready(rail) &&
		rail->peer->xd->local_uuid && rail->peer->xd->remote_uuid;
}

static long tbv_gpu_topology(void __user *arg)
{
	struct tbv_gpu_topology topology = { .version = TBV_GPU_ABI_VERSION };
	struct tbv_peer *peer;

	mutex_lock(&gpu_state->lock);
	list_for_each_entry(peer, &gpu_state->peers, node) {
		struct tbv_rail *rail;

		list_for_each_entry(rail, &peer->rails, node) {
			struct tbv_gpu_rail_info *info;

			if (!tbv_gpu_rail_ready(rail) ||
			    topology.count == TBV_GPU_MAX_RAILS)
				continue;
			info = &topology.rails[topology.count++];
			info->id.peer = peer->peer_id;
			info->id.rail = rail->rail_id;
			info->local_path = rail->path.local_transmit_path;
			info->remote_path = rail->remote_transmit_path;
			memcpy(info->local_domain, peer->xd->local_uuid, 16);
			memcpy(info->remote_domain, peer->xd->remote_uuid, 16);
			memcpy(info->local_host, gpu_state->native_host_id, 16);
			memcpy(info->remote_host, peer->remote_host_id, 16);
		}
	}
	mutex_unlock(&gpu_state->lock);
	return copy_to_user(arg, &topology, sizeof(topology)) ? -EFAULT : 0;
}

static int tbv_gpu_import(struct tbv_gpu_session *s,
			  const struct tbv_gpu_import *req)
{
	u32 i, j;
	int ret = -EINVAL;

	if (s->imported || s->aborted)
		return -EBUSY;
	if (req->version != TBV_GPU_ABI_VERSION || req->reserved ||
	    !req->count || req->count > TBV_GPU_MAX_RAILS ||
	    !req->bytes || req->bytes > TBV_GPU_MAX_POOL_BYTES ||
	    req->bytes % TBV_GPU_FRAME_BYTES)
		return -EINVAL;
	for (i = req->count; i < TBV_GPU_MAX_RAILS; i++)
		if (req->rails[i].peer || req->rails[i].rail)
			return -EINVAL;
	s->tx = dma_buf_get(req->tx_fd);
	if (IS_ERR(s->tx)) {
		ret = PTR_ERR(s->tx);
		s->tx = NULL;
		return ret;
	}
	s->rx = dma_buf_get(req->rx_fd);
	if (IS_ERR(s->rx)) {
		ret = PTR_ERR(s->rx);
		s->rx = NULL;
		goto fail;
	}
	/* Limit whole pinned objects, not just user-declared subranges. */
	if (s->tx == s->rx || s->tx->size != req->bytes ||
	    s->rx->size != req->bytes)
		goto fail;
	s->bytes = req->bytes;
	mutex_lock(&gpu_state->lock);
	for (i = 0; i < req->count; i++) {
		struct tbv_peer *peer;
		struct tbv_rail *found = NULL;

		list_for_each_entry(peer, &gpu_state->peers, node) {
			struct tbv_rail *rail;

			if (peer->peer_id != req->rails[i].peer)
				continue;
			list_for_each_entry(rail, &peer->rails, node)
				if (rail->rail_id == req->rails[i].rail &&
				    tbv_gpu_rail_ready(rail))
					found = rail;
		}
		if (!found) {
			ret = -ENOTCONN;
			break;
		}
		for (j = 0; j < i; j++)
			if (found == s->lanes[j].rail)
				break;
		if (j != i || (i && memcmp(found->peer->remote_host_id,
				s->lanes[0].rail->peer->remote_host_id, 16))) {
			ret = -EINVAL;
			break;
		}
		refcount_inc(&found->refcnt);
		s->lanes[s->count++].rail = found;
	}
	mutex_unlock(&gpu_state->lock);
	if (s->count != req->count)
		goto fail;
	for (i = 0; i < s->count; i++) {
		struct tbv_gpu_lane *lane = &s->lanes[i];
		struct tbv_path *path = &lane->rail->path;

		if (TBV_GPU_MAX_FRAMES_PER_RAIL >= path->cfg.tx_ring_size ||
		    TBV_GPU_MAX_FRAMES_PER_RAIL >= path->cfg.rx_ring_size) {
			ret = -EINVAL;
			goto fail;
		}
		lane->tx_frames = kcalloc(TBV_GPU_MAX_FRAMES_PER_RAIL,
					 sizeof(*lane->tx_frames), GFP_KERNEL);
		lane->rx_frames = kcalloc(TBV_GPU_MAX_FRAMES_PER_RAIL,
					 sizeof(*lane->rx_frames), GFP_KERNEL);
		if (!lane->tx_frames || !lane->rx_frames) {
			ret = -ENOMEM;
			goto fail;
		}
		ret = tbv_gpu_map(&lane->tx, s->tx,
				  tb_ring_dma_device(path->tx_ring), s->bytes,
				  DMA_TO_DEVICE);
		if (ret)
			goto fail;
		ret = tbv_gpu_map(&lane->rx, s->rx,
				  tb_ring_dma_device(path->rx_ring), s->bytes,
				  DMA_FROM_DEVICE);
		if (ret)
			goto fail;
	}
	s->imported = true;
	pr_info("imported %u rails, %llu bytes per direction, no CPU payload mapping\n",
		s->count, s->bytes);
	return 0;
fail:
	tbv_gpu_drop_import(s);
	return ret;
}

static void tbv_gpu_complete(struct tb_ring *ring, struct ring_frame *frame,
			     bool canceled)
{
	struct tbv_gpu_frame *f = container_of(frame, struct tbv_gpu_frame, frame);
	struct tbv_gpu_batch *batch = f->batch;
	struct tbv_path *path = &f->lane->rail->path;
	struct tbv_state *state = path->rail->peer->state;
	int error = 0;
#ifndef TBV_DISABLE_PROFILING
	bool profiled = f->lane->perf_active[!ring->is_tx];
	struct tbv_perf_id perf = {};
#endif

	if (canceled)
		error = -ECANCELED;
	else if (!ring->is_tx &&
		 (frame->size || frame->sof != TBV_DATA_PDF_FRAME_START ||
		  frame->eof != TBV_DATA_PDF_FRAME_END ||
		  (frame->flags & (RING_DESC_CRC_ERROR | RING_DESC_BUFFER_OVERRUN))))
		error = -EBADMSG;
	if (error)
		atomic_cmpxchg(&batch->error, 0, error);
#ifndef TBV_DISABLE_PROFILING
	if (profiled) {
		u32 direction = !ring->is_tx;
		int left = atomic_dec_return(&f->lane->perf_pending[direction]);

		/* WAIT can permit a new sequence as soon as pending reaches zero.
		 * Copy the identity before that publication, without retaining a
		 * pointer to frame/lane metadata after the final decrement. */
		perf = batch->perf;
		if (left == (int)f->lane->perf_count[direction] - 1)
			tbv_perf_mark(&f->lane->perf[direction], "gpu_dma_first", error, left);
		if (!left)
			tbv_perf_mark(&f->lane->perf[direction], "gpu_dma_last", error, 0);
	}
#endif
	if (canceled) {
		atomic64_inc(ring->is_tx ? &state->data_tx_canceled :
					 &state->data_rx_canceled);
		if (!ring->is_tx)
			atomic64_inc(&path->data_rx_canceled);
	} else if (ring->is_tx) {
		atomic64_inc(&state->data_tx_completed);
		atomic64_inc(&path->data_tx_completed);
		atomic64_add(TBV_GPU_FRAME_BYTES, &path->tx_bytes);
	} else {
		atomic64_inc(&state->data_rx_completed);
		atomic64_inc(&path->data_rx_completed);
		atomic64_add(frame->size ?: TBV_GPU_FRAME_BYTES, &path->rx_bytes);
		if (error)
			atomic64_inc(&state->data_rx_bad_frame);
	}
	if (atomic_dec_and_test(&batch->pending) || error) {
#ifndef TBV_DISABLE_PROFILING
		if (profiled)
			tbv_perf_mark(&perf, "gpu_batch_dma_done", error, 0);
#endif
		wake_up_all(&batch->wait);
	}
}

static int tbv_gpu_submit_impl(struct tbv_gpu_session *s,
			  const struct tbv_gpu_transfer *req, bool tx)
{
	struct tbv_gpu_batch *batch = tx ? &s->tx_batch : &s->rx_batch;
	u32 frames, i;
	int ret;

	if (s->aborted || !s->imported)
		return -ENOTCONN;
	if (batch->active)
		return -EBUSY;
	if (!req->sequence || req->sequence != batch->sequence + 1)
		return -EINVAL;
	ret = tbv_gpu_check_transfer(s->bytes, req->offset, req->bytes, s->count);
	if (ret)
		return ret;
	frames = req->bytes / TBV_GPU_FRAME_BYTES;
	s->submitted = true;
	batch->active = true;
	batch->sequence = req->sequence;
	batch->bytes = req->bytes;
	if (tbv_perf_clock())
		batch->perf = (struct tbv_perf_id) {
			.token = req->sequence, .wr_id = s->perf_session,
			.offset = req->offset, .bytes = req->bytes, .tx = tx,
		};
	atomic_set(&batch->error, 0);
	/* Count up front: callbacks can run before the enqueue loop finishes. */
	atomic_set(&batch->pending, frames);
	for (i = 0; i < s->count; i++) {
		struct tbv_gpu_lane *lane = &s->lanes[i];
		struct tb_ring *ring = tx ? lane->rail->path.tx_ring :
					   lane->rail->path.rx_ring;
		struct tbv_gpu_map *map = tx ? &lane->tx : &lane->rx;
		struct tbv_gpu_frame *slots = tx ? lane->tx_frames : lane->rx_frames;
		u32 first = tbv_gpu_block_start(frames, s->count, i);
		u32 end = tbv_gpu_block_start(frames, s->count, i + 1);
		u32 n;
		u64 perf_start = tbv_perf_clock();

		lane->perf_active[!tx] = !!perf_start;
		if (perf_start) {
			lane->perf[!tx] = batch->perf;
			lane->perf[!tx].rail = lane->rail->rail_id;
			lane->perf_count[!tx] = end - first;
			atomic_set(&lane->perf_pending[!tx], end - first);
		}

		for (n = first; n < end; n++) {
			struct tbv_gpu_frame *f = &slots[n - first];

			memset(f, 0, sizeof(*f));
			INIT_LIST_HEAD(&f->frame.list);
			f->batch = batch;
			f->lane = lane;
			f->frame.buffer_phy = map->frames[req->offset /
							TBV_GPU_FRAME_BYTES + n];
			f->frame.callback = tbv_gpu_complete;
			f->frame.sof = TBV_DATA_PDF_FRAME_START;
			f->frame.eof = TBV_DATA_PDF_FRAME_END;
			/* size=0 encodes 4096. Ring core requests the interrupt. */
			ret = tx ? tb_ring_tx(ring, &f->frame) : tb_ring_rx(ring, &f->frame);
			if (ret) {
				tbv_perf_span(&lane->perf[!tx], "gpu_lane_post", perf_start, ret, end - first);
				tbv_gpu_abort(s);
				return ret;
			}
			if (tx) {
				atomic64_inc(&gpu_state->data_tx_posted);
				atomic64_inc(&lane->rail->path.data_tx_posted);
			}
		}
		tbv_perf_span(&lane->perf[!tx], "gpu_lane_post", perf_start, 0, end - first);
	}
	return 0;
}

static int tbv_gpu_submit(struct tbv_gpu_session *s,
			  const struct tbv_gpu_transfer *req, bool tx)
{
	u64 start = tbv_perf_clock();
	struct tbv_perf_id id = {
		.token = req->sequence, .wr_id = s->perf_session,
		.offset = req->offset, .bytes = req->bytes, .tx = tx,
	};
	int ret = tbv_gpu_submit_impl(s, req, tx);

	tbv_perf_span(&id, "gpu_submit", start, ret, 0);
	return ret;
}

static long tbv_gpu_wait(struct tbv_gpu_session *s, void __user *arg)
{
	struct tbv_gpu_wait req;
	struct tbv_gpu_batch *batch;
	unsigned long deadline;
	bool kicked = false;
	long ret;
	u64 perf_start;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	if (req.reserved || req.completed_bytes || req.status ||
	    !req.timeout_ms || req.timeout_ms > 30000 ||
	    (req.direction != TBV_GPU_TX && req.direction != TBV_GPU_RX))
		return -EINVAL;
	batch = req.direction == TBV_GPU_TX ? &s->tx_batch : &s->rx_batch;
	if (!batch->active || req.sequence != batch->sequence)
		return -EINVAL;
	if (batch->waiting)
		return -EBUSY;
	/* Only one WAIT per direction; a second caller must not reap/reuse it. */
	batch->waiting = true;
	perf_start = tbv_perf_clock();
	deadline = jiffies + msecs_to_jiffies(req.timeout_ms);
	for (;;) {
		u32 i;

		if (!atomic_read(&batch->pending) || atomic_read(&batch->error)) {
			ret = 1;
			break;
		}
		if (time_after_eq(jiffies, deadline)) {
			ret = 0;
			break;
		}
		mutex_unlock(&gpu_lock);
		ret = wait_event_interruptible_timeout(batch->wait,
			!atomic_read(&batch->pending) || atomic_read(&batch->error),
			min_t(unsigned long, deadline - jiffies,
			      msecs_to_jiffies(10)));
		mutex_lock(&gpu_lock);
		if (ret)
			break;
		if (s->aborted || time_after_eq(jiffies, deadline))
			continue;
		/* Some NHI kernels can lose the final MSI-X notification. Use the
		 * same serialized worker to harvest completed descriptors; never
		 * manufacture completion or run a competing RX poll callback.
		 * gpu_lock keeps disconnect from freeing these rings while queued.
		 */
		tbv_perf_mark(&batch->perf, "gpu_dma_watchdog", 0, atomic_read(&batch->pending));
		for (i = 0; i < s->count; i++) {
			struct tbv_path *path = &s->lanes[i].rail->path;
			struct tb_ring *ring = req.direction == TBV_GPU_TX ?
				path->tx_ring : path->rx_ring;

			if (schedule_work(&ring->work)) {
				atomic64_inc(&gpu_work_kicks);
				kicked = true;
			}
		}
	}
	if (ret <= 0)
		req.status = ret ? (int)ret : -ETIMEDOUT;
	else
		req.status = atomic_read(&batch->error);
	if (req.status)
		tbv_gpu_abort(s);
	else {
		req.completed_bytes = batch->bytes;
		if (kicked)
			atomic64_inc(&gpu_wait_after_kick);
	}
	batch->active = false;
	batch->waiting = false;
	ret = copy_to_user(arg, &req, sizeof(req)) ? -EFAULT : 0;
	tbv_perf_span(&batch->perf, "gpu_wait", perf_start, ret ?: req.status, kicked);
	return ret;
}

static long tbv_gpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tbv_gpu_session *s = file->private_data;
	void __user *ptr = (void __user *)arg;
	long ret;
	u64 start, service;
	struct tbv_perf_id id = { .wr_id = s->perf_session };

	/* Privilege is checked per call, including after fd passing. */
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	start = tbv_perf_clock();
	mutex_lock(&gpu_lock);
	tbv_perf_span(&id, "gpu_ioctl_lock", start, 0, cmd);
	service = tbv_perf_clock();
	switch (cmd) {
	case TBV_GPU_GET_TOPOLOGY:
		ret = tbv_gpu_topology(ptr);
		break;
	case TBV_GPU_IMPORT: {
		struct tbv_gpu_import req;

		ret = copy_from_user(&req, ptr, sizeof(req)) ? -EFAULT :
			tbv_gpu_import(s, &req);
		break;
	}
	case TBV_GPU_PREPARE_RX:
	case TBV_GPU_SUBMIT_TX: {
		struct tbv_gpu_transfer req;

		ret = copy_from_user(&req, ptr, sizeof(req)) ? -EFAULT :
			tbv_gpu_submit(s, &req, cmd == TBV_GPU_SUBMIT_TX);
		break;
	}
	case TBV_GPU_WAIT:
		ret = tbv_gpu_wait(s, ptr);
		break;
	case TBV_GPU_ABORT:
		tbv_gpu_abort(s);
		ret = 0;
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&gpu_lock);
	tbv_perf_span(&id, "gpu_ioctl", service, ret, cmd);
	return ret;
}

static int tbv_gpu_open(struct inode *inode, struct file *file)
{
	struct tbv_gpu_session *s;
	int ret = 0;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	init_waitqueue_head(&s->tx_batch.wait);
	init_waitqueue_head(&s->rx_batch.wait);
	if (tbv_perf_clock())
		s->perf_session = atomic64_inc_return(&gpu_perf_session);
	mutex_lock(&gpu_lock);
	if (gpu_session) {
		ret = -EBUSY;
		kfree(s);
	} else {
		gpu_session = s;
		file->private_data = s;
	}
	mutex_unlock(&gpu_lock);
	return ret ?: nonseekable_open(inode, file);
}

static int tbv_gpu_release(struct inode *inode, struct file *file)
{
	mutex_lock(&gpu_lock);
	tbv_gpu_abort(file->private_data);
	kfree(file->private_data);
	gpu_session = NULL;
	mutex_unlock(&gpu_lock);
	return 0;
}

static const struct file_operations tbv_gpu_fops = {
	.owner = THIS_MODULE,
	.open = tbv_gpu_open,
	.release = tbv_gpu_release,
	.unlocked_ioctl = tbv_gpu_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
};

static struct miscdevice tbv_gpu_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tbv_gpu_direct",
	.fops = &tbv_gpu_fops,
	.mode = 0600,
};

int tbv_gpu_direct_start(struct tbv_state *state)
{
	if (!gpu_direct)
		return 0;
	gpu_state = state;
	return misc_register(&tbv_gpu_device);
}

void tbv_gpu_direct_stop(void)
{
	if (gpu_direct)
		misc_deregister(&tbv_gpu_device);
}

MODULE_IMPORT_NS("DMA_BUF");
