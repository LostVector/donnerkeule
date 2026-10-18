// SPDX-License-Identifier: GPL-2.0
#include <linux/highmem.h>
#include <linux/mmzone.h>
#include <linux/overflow.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "../proto/native_data.h"
#include "tbv.h"
#include "verbs_internal.h"

static atomic_t tbv_mr_key = ATOMIC_INIT(1);

struct tbv_mr *tbv_mr_get(struct tbv_state *state, u32 key)
{
	struct tbv_mr *mr;
	XA_STATE(xas, &state->verbs_mrs_xa, key);
	unsigned long flags;

	xas_lock_irqsave(&xas, flags);
	mr = xas_load(&xas);
	if (mr && !mr->closing && refcount_inc_not_zero(&mr->refs))
		goto out;
	mr = NULL;
out:
	xas_unlock_irqrestore(&xas, flags);
	return mr;
}

static void tbv_mr_free(struct tbv_mr *mr)
{
	tbv_dmabuf_release(mr->dmabuf);
	kvfree(mr->pages);
	if (mr->umem)
		ib_umem_release(mr->umem);
	if (mr->owner)
		atomic_dec(&mr->owner->verbs_mrs);
	kfree(mr);
}

static void tbv_mr_free_work(struct work_struct *work)
{
	struct tbv_mr *mr = container_of(work, struct tbv_mr, free_work);

	tbv_mr_free(mr);
}

void tbv_mr_put(struct tbv_mr *mr)
{
	if (!mr)
		return;

	if (refcount_dec_and_test(&mr->refs))
		queue_work(mr->owner && mr->owner->workqueue ?
			   mr->owner->workqueue : system_unbound_wq,
			   &mr->free_work);
}

/*
 * Lists the umem's pages in order. The index is optional: a scatterlist
 * that does not start page-aligned and consist of whole pages (other than
 * its last entry) leaves the MR unindexed, and copies walk the list.
 */

static void tbv_mr_index_pages(struct tbv_mr *mr)
{
	struct sg_table *sgt = &mr->umem->sgt_append.sgt;
	unsigned long npages = 0;
	struct scatterlist *sg;
	struct page **pages;
	unsigned int i;

	for_each_sgtable_sg(sgt, sg, i) {
		if (sg->offset ||
		    (!sg_is_last(sg) && !PAGE_ALIGNED(sg->length)))
			return;
		npages += DIV_ROUND_UP(sg->length, PAGE_SIZE);
	}
	if (!npages)
		return;

	pages = kvmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return;
	npages = 0;
	for_each_sgtable_sg(sgt, sg, i) {
		unsigned long n = DIV_ROUND_UP(sg->length, PAGE_SIZE);
		unsigned long j;

		for (j = 0; j < n; j++)
			pages[npages++] =
				pfn_to_page(page_to_pfn(sg_page(sg)) + j);
	}
	mr->pages = pages;
	mr->npages = npages;
}

/*
 * Copies @len bytes between @buf and an indexed MR at @offset, counted from
 * the start of the umem's first page (ib_umem_offset() plus the offset from
 * the MR start). Returns -ENOENT for an unindexed MR.
 */

int tbv_mr_copy_pages(struct tbv_mr *mr, size_t offset, void *buf,
			     size_t len, bool to_mr)
{
	if (!mr->pages)
		return -ENOENT;

	while (len) {
		unsigned long idx = offset >> PAGE_SHIFT;
		size_t in_page = offset_in_page(offset);
		size_t chunk = min_t(size_t, len, PAGE_SIZE - in_page);
		struct page *page;
		u8 *va;

		if (idx >= mr->npages)
			return -EFAULT;
		page = mr->pages[idx];
		va = kmap_local_page(page);
		if (to_mr) {
			memcpy(va + in_page, buf, chunk);
			flush_dcache_page(page);
		} else {
			memcpy(buf, va + in_page, chunk);
		}
		kunmap_local(va);
		buf = (u8 *)buf + chunk;
		offset += chunk;
		len -= chunk;
	}
	return 0;
}

int tbv_umem_copy_to(struct tbv_mr *mr, u64 addr, const void *src,
			    size_t len)
{
	struct sg_table *sgt;
	size_t offset;
	size_t copied;
	u64 mr_end;
	u64 end;
	int ret;

	if (!len)
		return 0;
	if (mr->dma_mr || mr->dmabuf)
		return -EOPNOTSUPP;
	sgt = &mr->umem->sgt_append.sgt;
	if (check_add_overflow(addr, (u64)len, &end))
		return -EINVAL;
	if (check_add_overflow(mr->start, mr->length, &mr_end))
		return -EINVAL;
	if (addr < mr->start || end > mr_end)
		return -EFAULT;

	offset = ib_umem_offset(mr->umem) + addr - mr->start;
	ret = tbv_mr_copy_pages(mr, offset, (void *)src, len, true);
	if (ret != -ENOENT)
		return ret;
	copied = sg_pcopy_from_buffer(sgt->sgl, sgt->orig_nents, src, len,
				      offset);
	return copied == len ? 0 : -EFAULT;
}

int tbv_umem_copy_to_iova(struct tbv_mr *mr, u64 iova,
				 const void *src, size_t len)
{
	u64 iova_end;
	u64 mr_iova_end;
	u64 addr;

	if (!len)
		return 0;
	if (mr->dma_mr)
		return -EOPNOTSUPP;
	if (check_add_overflow(iova, (u64)len, &iova_end))
		return -EINVAL;
	if (check_add_overflow(mr->virt_addr, mr->length, &mr_iova_end))
		return -EINVAL;
	if (iova < mr->virt_addr || iova_end > mr_iova_end)
		return -EFAULT;
	if (check_add_overflow(mr->start, iova - mr->virt_addr, &addr))
		return -EINVAL;

	return tbv_umem_copy_to(mr, addr, src, len);
}

int tbv_umem_iova_to_addr(struct tbv_mr *mr, u64 iova, size_t len,
				 u64 *addr_out)
{
	u64 iova_end;
	u64 mr_iova_end;
	u64 addr;

	if (mr->dma_mr || mr->dmabuf)
		return -EOPNOTSUPP;
	if (check_add_overflow(iova, (u64)len, &iova_end))
		return -EINVAL;
	if (check_add_overflow(mr->virt_addr, mr->length, &mr_iova_end))
		return -EINVAL;
	if (iova < mr->virt_addr || iova_end > mr_iova_end)
		return -EFAULT;
	if (check_add_overflow(mr->start, iova - mr->virt_addr, &addr))
		return -EINVAL;

	*addr_out = addr;
	return 0;
}

int tbv_umem_copy_from_iova(struct tbv_mr *mr, u64 iova,
				   void *dst, size_t len)
{
	u64 addr;
	int ret;

	if (!len)
		return 0;
	ret = tbv_umem_iova_to_addr(mr, iova, len, &addr);
	if (ret)
		return ret;

	ret = tbv_mr_copy_pages(mr, ib_umem_offset(mr->umem) + addr - mr->start,
				dst, len, false);
	if (ret != -ENOENT)
		return ret;
	return ib_umem_copy_from(dst, mr->umem, addr - mr->start, len);
}

int tbv_umem_page_from_addr(struct tbv_mr *mr, u64 addr, u32 max_len,
				   struct page **page_out,
				   u32 *page_off_out, u32 *len_out)
{
	struct sg_table *sgt;
	struct scatterlist *sg;
	size_t offset;
	u64 end;
	u64 mr_end;
	unsigned int i;

	if (!max_len)
		return -EINVAL;
	if (mr->dma_mr || mr->dmabuf)
		return -EOPNOTSUPP;
	sgt = &mr->umem->sgt_append.sgt;
	if (check_add_overflow(addr, (u64)max_len, &end))
		return -EINVAL;
	if (check_add_overflow(mr->start, mr->length, &mr_end))
		return -EINVAL;
	if (addr < mr->start || end > mr_end)
		return -EFAULT;

	offset = ib_umem_offset(mr->umem) + addr - mr->start;
	if (mr->pages) {
		unsigned long idx = offset >> PAGE_SHIFT;
		size_t page_off = offset_in_page(offset);
		size_t chunk = min_t(size_t, PAGE_SIZE - page_off, max_len);

		chunk = min_t(size_t, chunk, TBV_NATIVE_DATA_FRAME_SIZE);
		if (idx >= mr->npages || !chunk)
			return -EFAULT;
		*page_out = mr->pages[idx];
		*page_off_out = page_off;
		*len_out = chunk;
		return 0;
	}
	for_each_sgtable_sg(sgt, sg, i) {
		size_t seg_len = sg->length;
		size_t seg_off;
		size_t page_off;
		size_t chunk;

		if (offset >= seg_len) {
			offset -= seg_len;
			continue;
		}

		seg_off = sg->offset + offset;
		page_off = offset_in_page(seg_off);
		chunk = min_t(size_t, PAGE_SIZE - page_off, seg_len - offset);
		chunk = min_t(size_t, chunk, max_len);
		chunk = min_t(size_t, chunk, TBV_NATIVE_DATA_FRAME_SIZE);
		if (!chunk)
			return -EFAULT;

		*page_out = pfn_to_page(page_to_pfn(sg_page(sg)) +
					(seg_off >> PAGE_SHIFT));
		*page_off_out = page_off;
		*len_out = chunk;
		return 0;
	}

	return -EFAULT;
}

static int tbv_mr_publish(struct tbv_mr *mr, struct ib_pd *pd)
{
	struct tbv_state *owner = tbv_ibdev_state(pd->device);
	unsigned long flags;
	u32 key;
	int ret;

	key = atomic_inc_return(&tbv_mr_key);
	mr->base.device = pd->device;
	mr->base.pd = pd;
	mr->base.lkey = key;
	mr->base.rkey = key;
	mr->owner = owner;
	refcount_set(&mr->refs, 1);
	INIT_WORK(&mr->free_work, tbv_mr_free_work);

	xa_lock_irqsave(&owner->verbs_mrs_xa, flags);
	ret = __xa_insert(&owner->verbs_mrs_xa, key, mr, GFP_KERNEL);
	xa_unlock_irqrestore(&owner->verbs_mrs_xa, flags);
	if (ret)
		return ret;

	atomic_inc(&owner->verbs_mrs);
	return 0;
}

struct ib_mr *tbv_get_dma_mr(struct ib_pd *pd, int access)
{
	struct tbv_mr *mr;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->base.type = IB_MR_TYPE_DMA;
	mr->base.iova = 0;
	mr->base.length = U64_MAX;
	mr->start = 0;
	mr->length = U64_MAX;
	mr->virt_addr = 0;
	mr->access = access;
	mr->dma_mr = true;

	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		kfree(mr);
		return ERR_PTR(ret);
	}

	return &mr->base;
}

struct ib_mr *tbv_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				     u64 virt_addr, int access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
				     struct ib_dmah *dmah,
#endif
				     struct ib_udata *udata)
{
	struct tbv_mr *mr;
	int ret;

	if (!length)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 2, 0)
	mr->umem = ib_umem_get_va(pd->device, start, length, access);
#else
	mr->umem = ib_umem_get(pd->device, start, length, access);
#endif
	if (IS_ERR(mr->umem)) {
		struct ib_umem *umem = mr->umem;

		kfree(mr);
		return ERR_CAST(umem);
	}

	mr->base.type = IB_MR_TYPE_USER;
	mr->base.iova = virt_addr;
	mr->base.length = length;
	mr->start = start;
	mr->length = length;
	mr->virt_addr = virt_addr;
	mr->access = access;
	tbv_mr_index_pages(mr);
	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		kvfree(mr->pages);
		ib_umem_release(mr->umem);
		kfree(mr);
		return ERR_PTR(ret);
	}
	return &mr->base;
}

struct ib_mr *tbv_reg_user_mr_dmabuf(struct ib_pd *pd, u64 offset,
					   u64 length, u64 virt_addr, int fd,
					   int access,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
					   struct ib_dmah *dmah,
#endif
					   struct uverbs_attr_bundle *attrs)
{
	struct tbv_mr *mr;
	u64 end;
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);
#endif
	if (!tbv_dmabuf_verbs_enabled() || tbv_ibdev_backend(pd->device) != TBV_BACKEND_NATIVE ||
	    access & ~(IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE))
		return ERR_PTR(-EOPNOTSUPP);
	if (!length || virt_addr % PAGE_SIZE ||
	    check_add_overflow(virt_addr, length, &end) ||
	    ((access & IB_ACCESS_REMOTE_WRITE) && !(access & IB_ACCESS_LOCAL_WRITE)))
		return ERR_PTR(-EINVAL);
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);
	mr->dmabuf = tbv_dmabuf_import(pd->device, offset, length, fd, access);
	if (IS_ERR(mr->dmabuf)) {
		ret = PTR_ERR(mr->dmabuf);
		kfree(mr);
		return ERR_PTR(ret);
	}
	mr->base.type = IB_MR_TYPE_USER;
	mr->base.iova = virt_addr;
	mr->base.length = length;
	mr->start = virt_addr;
	mr->virt_addr = virt_addr;
	mr->length = length;
	mr->access = access;
	ret = tbv_mr_publish(mr, pd);
	if (ret) {
		tbv_dmabuf_release(mr->dmabuf);
		kfree(mr);
		return ERR_PTR(ret);
	}
	return &mr->base;
}

int tbv_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct tbv_mr *mr = container_of(ibmr, struct tbv_mr, base);
	unsigned long flags;

	if (mr->owner) {
		xa_lock_irqsave(&mr->owner->verbs_mrs_xa, flags);
		mr->closing = true;
		__xa_erase(&mr->owner->verbs_mrs_xa, ibmr->lkey);
		xa_unlock_irqrestore(&mr->owner->verbs_mrs_xa, flags);
	}

	tbv_mr_put(mr);
	return 0;
}
