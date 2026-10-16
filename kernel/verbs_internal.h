// SPDX-License-Identifier: GPL-2.0
#ifndef TBV_VERBS_INTERNAL_H
#define TBV_VERBS_INTERNAL_H

#include "tbv.h"
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>

struct tbv_state;
struct tbv_dmabuf_mr;

#define TBV_IBDEV_MAX_CQE 4096

/* Private layouts shared by object allocation and the data path. CQ entries
 * and notifications are protected by lock. MR lookup/closing is protected
 * by owner->verbs_mrs_xa; each data-path use holds refs until completion. */
struct tbv_cq {
	struct ib_cq base;
	struct tbv_state *owner;
	spinlock_t lock;
	struct ib_wc *entries;
	u32 cqe;
	u32 head;
	u32 tail;
	u32 count;
	bool notify_armed;
	bool overflowed;
};

struct tbv_mr {
	struct ib_mr base;
	struct tbv_state *owner;
	struct ib_umem *umem;
	struct tbv_dmabuf_mr *dmabuf;
	/*
	 * The umem's pages in order, so a copy finds the page of an offset
	 * directly; scatterlist copy helpers walk from the first entry, which
	 * costs one step per page before the offset. NULL when unindexed.
	 */
	struct page **pages;
	unsigned long npages;
	refcount_t refs;
	struct work_struct free_work;
	u64 start;
	u64 length;
	u64 virt_addr;
	int access;
	bool closing;
	bool dma_mr;
};

struct tbv_state *tbv_ibdev_state(struct ib_device *ibdev);
enum tbv_backend_type tbv_ibdev_backend(struct ib_device *ibdev);
void tbv_ibdev_cq_overflow(struct ib_qp *qp);

struct tbv_mr *tbv_mr_get(struct tbv_state *state, u32 key);

void tbv_mr_put(struct tbv_mr *mr);

int tbv_mr_copy_pages(struct tbv_mr *mr, size_t offset, void *buf,
			     size_t len, bool to_mr);

int tbv_umem_copy_to(struct tbv_mr *mr, u64 addr, const void *src,
			    size_t len);

int tbv_umem_copy_to_iova(struct tbv_mr *mr, u64 iova,
				 const void *src, size_t len);

int tbv_umem_iova_to_addr(struct tbv_mr *mr, u64 iova, size_t len,
				 u64 *addr_out);

int tbv_umem_copy_from_iova(struct tbv_mr *mr, u64 iova,
				   void *dst, size_t len);

int tbv_umem_page_from_addr(struct tbv_mr *mr, u64 addr, u32 max_len,
				   struct page **page_out,
				   u32 *page_off_out, u32 *len_out);

struct ib_mr *tbv_get_dma_mr(struct ib_pd *pd, int access);

struct ib_mr *tbv_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				     u64 virt_addr, int access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
				     struct ib_dmah *dmah,
#endif
				     struct ib_udata *udata);

struct ib_mr *tbv_reg_user_mr_dmabuf(struct ib_pd *pd, u64 offset,
					   u64 length, u64 virt_addr, int fd,
					   int access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
					   struct ib_dmah *dmah,
#endif
					   struct uverbs_attr_bundle *attrs);

int tbv_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);

int tbv_create_cq(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
			 struct uverbs_attr_bundle *attrs);

int tbv_destroy_cq(struct ib_cq *cq, struct ib_udata *udata);

int tbv_cq_push(struct tbv_cq *tcq, const struct ib_wc *wc);

int tbv_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);

int tbv_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);

#endif
