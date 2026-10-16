// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include "tbv.h"
#include "verbs_internal.h"
#include "perf.h"

int tbv_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
			 struct uverbs_attr_bundle *attrs)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);

	if (!attr || attr->cqe <= 0 || attr->cqe > TBV_IBDEV_MAX_CQE)
		return -EINVAL;

	tcq->entries = kcalloc(attr->cqe, sizeof(*tcq->entries), GFP_KERNEL);
	if (!tcq->entries)
		return -ENOMEM;

	spin_lock_init(&tcq->lock);
	tcq->owner = tbv_ibdev_state(cq->device);
	tcq->cqe = attr->cqe;
	atomic_inc(&tcq->owner->verbs_cqs);
	return 0;
}

int tbv_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);

	if (tcq->owner)
		atomic_dec(&tcq->owner->verbs_cqs);
	kfree(tcq->entries);
	return 0;
}

int tbv_cq_push(struct tbv_cq *tcq, const struct ib_wc *wc)
{
	struct ib_qp *overflow_qp = NULL;
	unsigned long flags;
	bool notify = false;
	int ret = 0;
	u64 start = tbv_perf_clock();
	struct tbv_perf_id perf = {};

	if (start && wc) {
		perf.src_qp = wc->qp ? wc->qp->qp_num : 0;
		perf.wr_id = wc->wr_id;
		perf.bytes = wc->byte_len;
		perf.opcode = wc->opcode;
		perf.tx = wc->opcode < IB_WC_RECV;
	}

	spin_lock_irqsave(&tcq->lock, flags);
	if (tcq->overflowed || tcq->count == tcq->cqe) {
		tcq->overflowed = true;
		if (tcq->owner)
			atomic64_inc(&tcq->owner->data_cq_overflow);
		if (tcq->notify_armed) {
			tcq->notify_armed = false;
			notify = true;
		}
		if (wc && wc->qp)
			overflow_qp = wc->qp;
		ret = -ENOSPC;
		goto out;
	}

	/* Timestamp before publishing: even a concurrently observed CQ entry
	 * cannot precede this marker. It includes the small publication cost. */
	tbv_perf_mark(&perf, "cq_publish", wc->status != IB_WC_SUCCESS ? -EIO : 0, 0);
	tcq->entries[tcq->tail] = *wc;
	tcq->tail = (tcq->tail + 1) % tcq->cqe;
	tcq->count++;
	if (tcq->notify_armed) {
		tcq->notify_armed = false;
		notify = true;
	}
out:
	spin_unlock_irqrestore(&tcq->lock, flags);
	tbv_perf_span(&perf, "cq_push", start,
		      ret ?: (wc && wc->status != IB_WC_SUCCESS ? -EIO : 0),
		      (wc ? wc->status << 1 : 0) | notify);
	if (overflow_qp)
		tbv_ibdev_cq_overflow(overflow_qp);
	start = tbv_perf_clock();
	if (notify && tcq->base.comp_handler) {
		tcq->base.comp_handler(&tcq->base, tcq->base.cq_context);
		tbv_perf_span(&perf, "cq_notify", start, ret, 0);
	}
	return ret;
}

int tbv_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);
	unsigned long flags;
	int polled = 0;
	bool overflowed;

	if (num_entries <= 0 || !wc)
		return 0;

	spin_lock_irqsave(&tcq->lock, flags);
	while (polled < num_entries && tcq->count) {
		wc[polled++] = tcq->entries[tcq->head];
		tcq->head = (tcq->head + 1) % tcq->cqe;
		tcq->count--;
	}
	overflowed = tcq->overflowed;
	spin_unlock_irqrestore(&tcq->lock, flags);

	if (!polled && overflowed)
		return -EIO;
	return polled;
}

int tbv_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags)
{
	struct tbv_cq *tcq = container_of(cq, struct tbv_cq, base);
	unsigned long irq_flags;
	int ret;

	spin_lock_irqsave(&tcq->lock, irq_flags);
	if (tcq->overflowed) {
		ret = -EIO;
	} else {
		tcq->notify_armed = true;
		ret = tcq->count ? 1 : 0;
	}
	spin_unlock_irqrestore(&tcq->lock, irq_flags);
	return ret;
}
