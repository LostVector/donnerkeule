/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the candidate's real helpers against explicit DMA-API outcomes. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64, dma_addr_t;
typedef uint32_t u32;
#define SZ_4K 4096UL
#define PAGE_SIZE SZ_4K
#define SZ_64K (64UL * 1024)
#define SZ_256M (256UL * 1024 * 1024)
#define PAGE_ALIGNED(x) (!((x) & (PAGE_SIZE - 1)))
#define DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define GFP_KERNEL 0
#define DMA_BIDIRECTIONAL 0
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
#define PTR_ERR(x) ((int)(intptr_t)(x))

struct device { int refs; };
struct page { int unused; };
struct mutex { bool locked; };
struct scatterlist { dma_addr_t address; u32 length; };
struct sg_table { struct scatterlist *sgl; unsigned int nents; bool mapped; };
struct tbv_host_dma_map {
    struct device *dev;
    struct sg_table sgt;
    dma_addr_t *pages;
    unsigned long npages;
};
struct tbv_mr {
    void *umem, *dmabuf;
    bool dma_mr;
    struct page **pages;
    unsigned long npages;
    u64 start, length;
    struct mutex host_dma_lock;
    struct tbv_host_dma_map host_dma[4];
};
static bool dmabuf_host_direct = true;
static unsigned int dmabuf_host_direct_min_bytes = SZ_64K;
static int host_dma_maps, host_dma_active;
static int sg_live, index_live, dma_live, alloc_error, map_error, index_error;
static int mapped_shape; /* 0: coalesced; 1: split/noncontiguous; 2..5 malformed. */
static void atomic64_inc(int *p) { ++*p; }
static void atomic64_dec(int *p) { --*p; }
static void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = true; }
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = false; }
static void mutex_destroy(struct mutex *m) { assert(!m->locked); }
static struct device *get_device(struct device *d) { ++d->refs; return d; }
static void put_device(struct device *d) { assert(d->refs > 0); --d->refs; }
static int sg_alloc_table_from_pages(struct sg_table *s, struct page **pages,
                                     unsigned long n, unsigned int off, unsigned long len, int flags)
{
    (void)pages; (void)flags;
    assert(n && off == 0 && len == n * PAGE_SIZE);
    if (alloc_error) return -ENOMEM;
    s->sgl = calloc(2, sizeof(*s->sgl)); assert(s->sgl); ++sg_live;
    s->nents = 1;
    s->sgl[0] = (struct scatterlist){0x100000, len};
    if (mapped_shape == 1) {
        s->nents = 2;
        s->sgl[0].length = len / 2;
        s->sgl[1] = (struct scatterlist){0x900000, len / 2};
    }
    if (mapped_shape == 2) s->sgl[0].address++;
    if (mapped_shape == 3) s->sgl[0].length--;
    if (mapped_shape == 4) s->sgl[0].length -= PAGE_SIZE;
    if (mapped_shape == 5) s->sgl[0].length += PAGE_SIZE;
    if (mapped_shape == 6) s->sgl[0].address = UINT64_MAX - PAGE_SIZE + 1;
    return 0;
}
static int dma_map_sgtable(struct device *dev, struct sg_table *s, int dir, int attrs)
{
    (void)dev; (void)s; (void)dir; (void)attrs;
    if (map_error) return -EIO;
    assert(!s->mapped); s->mapped = true; ++dma_live; return 0;
}
static void dma_unmap_sgtable(struct device *d, struct sg_table *s, int dir, int attrs)
{
    (void)d; (void)s; (void)dir; (void)attrs;
    assert(dma_live > 0 && s->mapped); s->mapped = false; --dma_live;
}
static void sg_free_table(struct sg_table *s) { assert(sg_live > 0 && !s->mapped); --sg_live; free(s->sgl); }
static void *kvmalloc_array(unsigned long n, unsigned long bytes, int flags)
{
    (void)flags;
    if (index_error) return NULL;
    void *p = calloc(n, bytes); assert(p); ++index_live; return p;
}
static void kvfree(void *p) { assert(p && index_live > 0); --index_live; free(p); }
#define for_each_sgtable_dma_sg(t, s, i) \
    for ((i) = 0; (i) < (t)->nents && (((s) = &(t)->sgl[i]), true); (i)++)
#define sg_dma_address(s) ((s)->address)
#define sg_dma_len(s) ((s)->length)
#include "host-direct-helpers.c"

static struct page *page_storage[32];
static struct tbv_mr mr_new(void)
{
    return (struct tbv_mr){.umem = (void *)1, .pages = page_storage, .npages = 32,
                          .start = 0x800000, .length = 32 * PAGE_SIZE};
}
static void clean(void)
{
    assert(!sg_live && !index_live && !dma_live && !host_dma_active);
}
int main(void)
{
    struct tbv_mr mr = mr_new();
    assert(!tbv_host_dma_eligible(&mr, 0, 5 * PAGE_SIZE));
    dmabuf_host_direct_min_bytes = PAGE_SIZE;
    assert(tbv_host_dma_eligible(&mr, 0, PAGE_SIZE));
    assert(tbv_host_dma_eligible(&mr, PAGE_SIZE, 5 * PAGE_SIZE));
    assert(!tbv_host_dma_eligible(&mr, 0, 0));
    assert(!tbv_host_dma_eligible(&mr, 0, PAGE_SIZE - 1));
    assert(!tbv_host_dma_eligible(&mr, 1, PAGE_SIZE));
    assert(!tbv_host_dma_eligible(&mr, 31 * PAGE_SIZE, 2 * PAGE_SIZE));
    const unsigned bad_thresholds[] = {0, PAGE_SIZE - 1, PAGE_SIZE + 1, SZ_64K + PAGE_SIZE};
    for (unsigned i = 0; i < ARRAY_SIZE(bad_thresholds); ++i) {
        dmabuf_host_direct_min_bytes = bad_thresholds[i];
        assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K));
    }
    dmabuf_host_direct_min_bytes = SZ_64K;
    assert(tbv_host_dma_eligible(&mr, 0, SZ_64K));
    assert(tbv_host_dma_eligible(&mr, SZ_64K, SZ_64K));
    assert(!tbv_host_dma_eligible(NULL, 0, SZ_64K));
    assert(!tbv_host_dma_eligible(&mr, 1, SZ_64K));
    assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K - 1));
    assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K + 1));
    assert(!tbv_host_dma_eligible(&mr, SZ_64K + PAGE_SIZE, SZ_64K));
    assert(!tbv_host_dma_eligible(&mr, UINT64_MAX & ~(PAGE_SIZE - 1), SZ_64K));
    mr.start++; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.start--;
    mr.length = SZ_256M + 1; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr = mr_new();
    mr.npages--; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.npages++;
    mr.dma_mr = true; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.dma_mr = false;
    mr.dmabuf = (void *)1; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.dmabuf = NULL;
    mr.umem = NULL; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.umem = (void *)1;
    mr.pages = NULL; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); mr.pages = page_storage;
    dmabuf_host_direct = false; assert(!tbv_host_dma_eligible(&mr, 0, SZ_64K)); dmabuf_host_direct = true;
    struct device dev[5] = {0};
    for (int shape = 0; shape < 2; ++shape) {
        mapped_shape = shape;
        struct tbv_host_dma_map *map = tbv_host_dma_map(&mr, &dev[0]);
        assert(!IS_ERR(map) && map->npages == mr.npages && dev[0].refs == 1);
        int before = host_dma_maps;
        assert(tbv_host_dma_map(&mr, &dev[0]) == map && host_dma_maps == before);
        dma_addr_t address = 0;
        assert(tbv_host_dma_address(&mr, map, 31 * PAGE_SIZE, PAGE_SIZE, &address) == 0);
        assert(address == (shape ? 0x900000 + 15 * PAGE_SIZE : 0x100000 + 31 * PAGE_SIZE));
        assert(tbv_host_dma_address(&mr, map, mr.length, 1, &address) == -EFAULT);
        assert(tbv_host_dma_address(&mr, map, 1, PAGE_SIZE, &address) == -EFAULT);
        assert(tbv_host_dma_address(&mr, map, 0, PAGE_SIZE + 1, &address) == -EFAULT);
        assert(tbv_host_dma_address(&mr, map, 0, 0, &address) == -EFAULT);
        mr.length--; assert(tbv_host_dma_address(&mr, map, 31 * PAGE_SIZE, PAGE_SIZE, &address) == -EFAULT); mr.length++;
        for (int i = 1; i < 4; ++i) assert(!IS_ERR(tbv_host_dma_map(&mr, &dev[i])));
        assert(PTR_ERR(tbv_host_dma_map(&mr, &dev[4])) == -ENOSPC);
        assert(host_dma_active == 4 && dma_live == 4);
        tbv_host_dma_release(&mr); clean();
        for (int i = 0; i < 5; ++i) assert(!dev[i].refs);
        mr = mr_new();
    }
    mapped_shape = 0;
    for (int failure = 0; failure < 8; ++failure) {
        alloc_error = failure == 0; map_error = failure == 1; index_error = failure == 2;
        mapped_shape = failure < 3 ? 0 : failure - 1;
        assert(IS_ERR(tbv_host_dma_map(&mr, &dev[0])));
        assert(!dev[0].refs && !mr.host_dma[0].dev && !mr.host_dma_lock.locked); clean();
        alloc_error = map_error = index_error = mapped_shape = 0;
        assert(!IS_ERR(tbv_host_dma_map(&mr, &dev[0])));
        tbv_host_dma_release(&mr); clean(); mr = mr_new();
    }
    return 0;
}
