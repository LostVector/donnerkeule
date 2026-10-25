/* SPDX-License-Identifier: GPL-2.0-only */
/* User-space boundary test; does not claim to simulate concurrent DMA. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t __le64;
typedef uint32_t __le32;
typedef int64_t atomic64_t;
#define CONFIG_X86_64 1
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(7,2,6)
/* kernel/verbs_dmabuf.c defines this for x86_64 Linux 7.2.x. */
#define TBV_BULK_NHI_ABI 1
#define __iomem
#define U16_MAX UINT16_MAX
#define PCI_VENDOR_ID_AMD 0x1022
#define RING_FLAG_FRAME 2
#define RING_DESC_POSTED 4
#define RING_DESC_INTERRUPT 8
#define GENMASK(h,l) (((UINT32_MAX)>>(31-(h)))&((UINT32_MAX)<<(l)))
#define BUILD_BUG_ON(c) _Static_assert(!(c), #c)
#define READ_ONCE(v) (v)
#define WRITE_ONCE(v,x) ((v)=(x))
#define cpu_to_le64(v) (v)
#define cpu_to_le32(v) (v)
#define le32_to_cpu(v) (v)
#define spin_lock_irqsave(lock,flags) ((void)(lock),(flags)=0)
#define spin_unlock_irqrestore(lock,flags) ((void)(lock),(void)(flags))
static void atomic64_inc(atomic64_t *v) { ++*v; }
static void atomic64_add(u32 n, atomic64_t *v) { *v += n; }
struct list_head { struct list_head *next, *prev; };
static void init_list(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(struct list_head *h) { return h->next==h; }
static void list_add_tail(struct list_head *n, struct list_head *h) {
    n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n;
}
struct pci_dev { unsigned vendor, device; };
struct device { bool pci; struct pci_dev pdev; };
static bool dev_is_pci(struct device *d) { return d->pci; }
static struct pci_dev *to_pci_dev(struct device *d) { return &d->pdev; }
struct tb_nhi { struct device *dev; void *iobase; int hop_count; bool going_away; };
struct tb_ring {
    uint64_t lock;
    struct tb_nhi *nhi;
    int size, hop, head, tail;
    void *descriptors;
    uint64_t descriptors_dma;
    struct list_head queue, in_flight;
    unsigned char unused[88];
    void *start_poll;
    unsigned flags;
    bool is_tx, running;
};
struct ring_frame { uint64_t buffer_phy; struct list_head list; u32 size, eof, sof; };
struct tbv_dmabuf_frame { struct ring_frame frame; };
struct state { atomic64_t data_tx_posted; };
struct peer { struct state *state; };
struct path { struct tb_ring *tx_ring, *rx_ring; atomic64_t data_tx_posted; };
struct rail { struct peer *peer; struct path path; };
struct tbv_dmabuf_lane { struct rail *rail; struct tbv_dmabuf_frame *frames; u32 count; };
static bool dmabuf_batch_doorbell;
static atomic64_t dmabuf_bulk_calls, dmabuf_bulk_frames, dmabuf_bulk_fallback;
static unsigned barriers, writes;
static u32 doorbell_value;
static void *doorbell_address;
static void dma_wmb(void) { ++barriers; }
static void iowrite32(u32 value, void *address) {
    assert(barriers > writes); ++writes; doorbell_value=value; doorbell_address=address;
}
#include "helper.c"

int main(void) {
    unsigned char mmio[0x10000]={0};
    struct device device={.pci=true,.pdev={.vendor=0x1022,.device=0x158d}};
    struct tb_nhi nhi={.dev=&device,.iobase=mmio,.hop_count=3};
    struct tbv_bulk_desc descriptors[8];
    struct tb_ring ring={.nhi=&nhi,.size=8,.hop=1,.head=6,.tail=3,
        .descriptors=descriptors,.flags=RING_FLAG_FRAME,.is_tx=true,.running=true};
    struct state state={0}; struct peer peer={.state=&state};
    struct rail rail={.peer=&peer,.path={.tx_ring=&ring,.rx_ring=&ring}};
    struct tbv_dmabuf_frame frames[4];
    struct tbv_dmabuf_lane lane={.rail=&rail,.frames=frames,.count=4};
    init_list(&ring.queue);init_list(&ring.in_flight);
    for (unsigned i=0;i<4;++i) {
        frames[i].frame=(struct ring_frame){.buffer_phy=0x1000u*(i+1),.size=i==3?32:0,.sof=1,.eof=i==3?3:2};
        init_list(&frames[i].frame.list);
    }
    memset(descriptors,0xa5,sizeof(descriptors));
    assert(tbv_dmabuf_post_bulk(&lane,true,0,0)==-EAGAIN);
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-EAGAIN && writes==0);
    dmabuf_batch_doorbell=true;
    assert(tbv_dmabuf_post_bulk(&lane,true,4,2)==-EINVAL && writes==0);
    ring.running=false;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-ESHUTDOWN && writes==0);
    ring.running=true;ring.tail=2;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-EAGAIN && writes==0 && ring.head==6);
    ring.tail=3;ring.start_poll=&ring;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-EAGAIN && writes==0);
    ring.start_poll=NULL;
    device.pdev.device=0xffff;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-EAGAIN && writes==0);
    device.pdev.device=0x158e;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==0);
    assert(writes==1 && ring.head==2 && ring.tail==3 && list_empty(&ring.queue));
    assert(doorbell_address==mmio+24 && doorbell_value==(2u<<16));
    assert(state.data_tx_posted==4 && rail.path.data_tx_posted==4 && dmabuf_bulk_frames==4);
    struct list_head *next=ring.in_flight.next;
    for (unsigned i=0;i<4;++i) {
        unsigned index=(6+i)%8;
        assert(next==&frames[i].frame.list); next=next->next;
        assert(descriptors[index].address==frames[i].frame.buffer_phy);
        assert(descriptors[index].metadata==(frames[i].frame.size|(frames[i].frame.eof<<12)|
            (1u<<16)|((unsigned)(i==3?12:4)<<20)));
        assert(descriptors[index].timestamp==0);
    }
    assert(next==&ring.in_flight);
    /* A frame already owned by a ring must never be appended twice. */
    ring.tail=7;
    assert(tbv_dmabuf_post_bulk(&lane,true,0,4)==-EINVAL && writes==1);
    init_list(&ring.in_flight);
    for(unsigned i=0;i<4;++i)init_list(&frames[i].frame.list);
    ring.is_tx=false;ring.head=0;ring.tail=0;
    u32 rx_metadata=descriptors[0].metadata&GENMASK(19,0);
    assert(tbv_dmabuf_post_bulk(&lane,false,0,4)==0);
    assert(writes==2 && doorbell_address==mmio+0x8000+24 && doorbell_value==4);
    assert((descriptors[0].metadata&GENMASK(19,0))==rx_metadata);
    assert(state.data_tx_posted==4 && dmabuf_bulk_calls==2);
    return 0;
}
