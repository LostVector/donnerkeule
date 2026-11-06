// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef u64 dma_addr_t;
typedef void (*tbv_path_tx_done_fn)(void *,int);
typedef long long atomic64_t;
#define atomic64_inc(p) (++*(p))
#define atomic_dec(p) (--*(p))
#define DMA_TO_DEVICE 0
#define TBV_DATA_FRAME_SIZE 4096
#define TBV_CONTROL_FRAME_SIZE 64
#define TBV_DATA_PDF_FRAME_START 1
#define TBV_DATA_PDF_FRAME_END 3
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static void take_lock(pthread_mutex_t*p){assert(!pthread_mutex_lock(p));}
static void drop_lock(pthread_mutex_t*p){assert(!pthread_mutex_unlock(p));}
#define spin_lock_irqsave(p,f) do{(f)=0;take_lock(p);}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);drop_lock(p);}while(0)
#define pr_warn_ratelimited(...) ((void)0)
struct list_head {struct list_head *next,*prev;};
static void init(struct list_head *p){p->next=p;p->prev=p;}
static bool list_empty(struct list_head*p){return p->next==p;}
static void list_add_tail(struct list_head*p,struct list_head*h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
static void list_del_init(struct list_head*p){p->prev->next=p->next;p->next->prev=p->prev;init(p);}
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
struct tb_ring {int unused;};
struct device {bool ready;};
struct ring_frame {unsigned size,flags,sof,eof;void *callback;};
struct tbv_path;
struct tbv_tx_packet {struct list_head node;struct tbv_path *path;u8 *buf;u32 len;dma_addr_t dma;tbv_path_tx_done_fn done;void *done_ctx,*owner_ctx;u8 sof,eof;u32 start_credit_group_frames;unsigned long queued_jiffies;bool control,pooled,queued,inflight,zcopy,unmap_dma,raw_stream_start,raw_stream_end,raw_stream_counted;u8 control_buf[64];};
struct tbv_state {atomic64_t data_tx_canceled,data_tx_completed;};
struct tbv_path {pthread_mutex_t tx_lock;struct list_head tx_data_free,tx_control_free,tx_free;struct tb_ring *tx_ring;atomic64_t control_tx_completed,control_tx_queue_max_ms,data_tx_completed;int tx_inflight;};
struct tbv_data_frame {struct ring_frame frame;struct tbv_path *path;struct tbv_tx_packet *packet;tbv_path_tx_done_fn done;void *done_ctx;dma_addr_t dma;struct list_head free_node;};
static struct device device;
static struct tbv_state state;
static bool attached=true;
static unsigned unmaps,frees,scheduled,raw_finished;
static unsigned long jiffies=100;
static struct tbv_state *tbv_path_state(struct tbv_path*p){(void)p;return attached?&state:NULL;}
static struct device *tb_ring_dma_device(struct tb_ring*r){(void)r;return &device;}
static bool tbv_dma_device_ready(struct device*d){return d->ready;}
static void dma_unmap_page(struct device*d,dma_addr_t a,u32 n,int dir){(void)d;(void)a;(void)n;(void)dir;unmaps++;}
static void dma_sync_single_for_cpu(struct device*d,dma_addr_t a,u32 n,int dir){(void)d;(void)a;(void)n;(void)dir;}
static void kfree(void*p){assert(p);frees++;}
static unsigned long jiffies_to_msecs(unsigned long n){return n;}
static void tbv_path_atomic64_max_ms(atomic64_t*p,u64 n){if((u64)*p<n)*p=n;}
static void tbv_path_finish_raw_stream_if_needed(struct tbv_path*p,struct tbv_tx_packet*q){(void)p;(void)q;raw_finished++;}
static void tbv_path_schedule_tx(struct tbv_path*p){assert(p->tx_inflight==0);scheduled++;}
#include "helpers.c"
static void setup(struct tbv_path*p){
 memset(p,0,sizeof(*p));assert(!pthread_mutex_init(&p->tx_lock,NULL));
 init(&p->tx_data_free);init(&p->tx_control_free);init(&p->tx_free);
 memset(&state,0,sizeof(state));device.ready=true;attached=true;
 unmaps=frees=scheduled=raw_finished=0;
}
static unsigned callbacks;
static int want_status;
static struct tbv_tx_packet *releasing;
static void callback(void *ctx,int status){
 assert(ctx==releasing&&status==want_status);
 assert(releasing->len==37&&releasing->owner_ctx==ctx);
 /* A one-entry pool must remain unavailable throughout its callback. */
 assert(!tbv_path_alloc_pooled_data_packet(releasing->path,1,NULL,NULL));
 callbacks++;
}
static void release_matrix(void){
 for(unsigned pooled=0;pooled<2;pooled++)for(unsigned control=0;control<2;control++)
 for(unsigned zcopy=0;zcopy<2;zcopy++)for(unsigned unmap=0;unmap<2;unmap++)
 for(unsigned ready=0;ready<2;ready++)for(unsigned done=0;done<2;done++)
 for(unsigned failed=0;failed<2;failed++){
  struct tbv_path path;setup(&path);u8 data[64];
  struct tbv_tx_packet packet={.path=&path,.buf=data,.len=37,.pooled=pooled,.control=control,.zcopy=zcopy,.unmap_dma=unmap,.inflight=true,.queued=true};
  init(&packet.node);releasing=&packet;packet.owner_ctx=&packet;
  packet.done_ctx=&packet;packet.done=done?callback:NULL;callbacks=0;
  device.ready=ready;want_status=failed?-ECANCELED:0;
  tbv_path_tx_packet_release(&packet,want_status);
  assert(callbacks==done&&unmaps==(zcopy&&unmap&&ready));
  assert(!packet.done&&!packet.done_ctx&&!packet.owner_ctx&&!packet.len&&!packet.inflight&&!packet.queued);
  const bool returned=pooled&&!zcopy;
  assert(list_empty(&path.tx_control_free)==!(returned&&control));
  assert(list_empty(&path.tx_data_free)==!(returned&&!control));
  assert(frees==(zcopy?1:(!control&&!pooled?2:(!pooled?1:0))));
  assert(!pthread_mutex_destroy(&path.tx_lock));
 }
}
static void completion_matrix(void){
 for(unsigned canceled=0;canceled<2;canceled++)for(unsigned packet_present=0;packet_present<2;packet_present++)
 for(unsigned control=0;control<2;control++)for(unsigned has_state=0;has_state<2;has_state++)
 for(unsigned queued=0;queued<2;queued++){
  struct tbv_path path;setup(&path);path.tx_inflight=1;attached=has_state;
  struct tbv_tx_packet packet={.path=&path,.pooled=true,.control=control,.queued_jiffies=queued?70:0};init(&packet.node);
  struct tbv_data_frame frame={.path=&path,.packet=packet_present?&packet:NULL,.done_ctx=&packet,.done=callback,.frame={.size=12,.flags=15,.sof=1,.eof=3,.callback=&packet}};init(&frame.free_node);
  struct tb_ring ring={0};tbv_path_tx_complete(&ring,&frame.frame,canceled);
  assert(!frame.packet&&!frame.done&&!frame.done_ctx&&!frame.frame.callback);
  assert(!frame.frame.size&&!frame.frame.flags&&!frame.frame.sof&&!frame.frame.eof);
  assert(!list_empty(&path.tx_free)&&!path.tx_inflight&&scheduled==1);
  assert(raw_finished==packet_present);
  assert(state.data_tx_canceled==(has_state&&canceled));
  assert(state.data_tx_completed==(has_state&&!canceled));
  assert(path.data_tx_completed==(packet_present&&!canceled&&!control));
  assert(path.control_tx_completed==(packet_present&&!canceled&&control));
  assert(path.control_tx_queue_max_ms==((packet_present&&!canceled&&control&&queued)?30:0));
  assert(!pthread_mutex_destroy(&path.tx_lock));
 }
}
static struct tbv_path shared;
static struct tbv_tx_packet slots[2];
static _Atomic bool owned[2];
static _Atomic unsigned completions;
static void concurrent_done(void *ctx,int status){
 struct tbv_tx_packet *p=ctx;assert(status==0);
 ptrdiff_t id=p-slots;assert(id>=0&&id<2);
 assert(atomic_exchange(&owned[id],false));atomic_fetch_add(&completions,1);
}
static void *worker(void *unused){
 (void)unused;
 for(unsigned i=0;i<5000;i++){
  struct tbv_tx_packet *p;
  while(!(p=tbv_path_alloc_pooled_data_packet(&shared,37,concurrent_done,NULL))){}
  ptrdiff_t id=p-slots;assert(id>=0&&id<2&&!atomic_exchange(&owned[id],true));
  assert(p->len==37&&!p->zcopy&&!p->unmap_dma&&!p->control&&!p->raw_stream_counted);
  p->done_ctx=p;p->owner_ctx=p;tbv_path_tx_packet_release(p,0);
 }
 return NULL;
}
static void reuse(void){
 setup(&shared);
 for(unsigned i=0;i<2;i++){slots[i]=(struct tbv_tx_packet){.path=&shared,.pooled=true};init(&slots[i].node);list_add_tail(&slots[i].node,&shared.tx_data_free);}
 pthread_t a,b;assert(!pthread_create(&a,NULL,worker,NULL));assert(!pthread_create(&b,NULL,worker,NULL));
 assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL));assert(completions==10000);
 assert(!owned[0]&&!owned[1]);assert(!pthread_mutex_destroy(&shared.tx_lock));
}
static int token;
static unsigned probe_calls;
static void probe(void*ctx,int status){assert(ctx==&token&&!status);probe_calls++;}
static void allocation_fields(void){
 struct tbv_path path;setup(&path);
 struct tbv_tx_packet p={.path=&path,.pooled=true,.control=true,.zcopy=true,.unmap_dma=true,.raw_stream_start=true,.raw_stream_end=true,.raw_stream_counted=true};
 init(&p.node);list_add_tail(&p.node,&path.tx_data_free);
 struct tbv_tx_packet *got=tbv_path_alloc_pooled_data_packet(&path,37,probe,&token);
 assert(got==&p&&p.done==probe&&p.done_ctx==&token&&p.owner_ctx==&token&&p.len==37);
 assert(!p.control&&!p.zcopy&&!p.unmap_dma&&!p.raw_stream_start&&!p.raw_stream_end&&!p.raw_stream_counted);
 assert(p.sof==TBV_DATA_PDF_FRAME_START&&p.eof==TBV_DATA_PDF_FRAME_END);
 assert(!tbv_path_alloc_pooled_data_packet(&path,1,NULL,NULL));
 tbv_path_tx_packet_release(got,0);assert(probe_calls==1);
 assert(tbv_path_alloc_pooled_data_packet(&path,1,NULL,NULL)==&p);
 tbv_path_tx_packet_release(&p,0);assert(probe_calls==1);
 assert(!pthread_mutex_destroy(&path.tx_lock));
}
int main(void){release_matrix();completion_matrix();allocation_fields();reuse();puts("PASS TX pool:128 release states,32 completion states,10000 concurrent allocations/releases; no early reuse or lost completion");return 0;}
