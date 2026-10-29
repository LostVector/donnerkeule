// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#define TBV_BACKEND_NATIVE 1
#define IB_QPT_RC 2
#define IB_QPS_ERR 9
#define IB_QPS_RESET 0
#define TBV_SEND_POST_INITIAL 0
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define to_delayed_work(p) container_of(p,struct delayed_work,work)
#define spin_lock_irqsave(p,f) do{(f)=0;assert(!pthread_mutex_lock(p));}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);assert(!pthread_mutex_unlock(p));}while(0)
struct list_head{struct list_head *next,*prev;};
static void init(struct list_head *p){p->next=p;p->prev=p;}
static bool list_empty(struct list_head *p){return p->next==p;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
static void list_del(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;init(p);}
static void list_move_tail(struct list_head *p,struct list_head *h){list_del(p);list_add_tail(p,h);}
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
struct work_struct{int unused;};struct delayed_work{struct work_struct work;bool pending;unsigned long delay;};
struct owner{int *workqueue;};struct tbv_send_ctx;
struct tbv_qp{int backend,type,state;bool closing;pthread_mutex_t lock;struct owner *owner;struct list_head pending_sends;struct delayed_work native_sq_work;_Atomic(struct tbv_send_ctx *) active;unsigned errors;};
struct tbv_send_ctx{struct tbv_qp *tqp;struct list_head node;bool pending,native_deferred,retrying,rnr_waiting,ready,sq_counted;_Atomic bool dead;unsigned long queued_jiffies,first_queued_jiffies;int retry_reason,completion_status,tx_pending;_Atomic int refs;int id,posts,offset,length;unsigned char value;};
static bool dmabuf;static int queue;static int *system_unbound_wq=&queue;static int post_error;
static bool tbv_dmabuf_verbs_enabled(void){return dmabuf;}
static bool queue_delayed_work(int *q,struct delayed_work *w,unsigned long d){assert(q);if(w->pending)return false;w->pending=true;w->delay=d;return true;}
static int atomic_read(int *n){return *n;}
static void tbv_send_ctx_get(struct tbv_send_ctx *s){assert(!s->dead && s->refs>0);s->refs++;}
static void tbv_send_ctx_put(struct tbv_send_ctx *s){assert(!s->dead && s->refs>0);if(!--s->refs)s->dead=true;}
static void tbv_qp_release_sendq_counted_locked(struct tbv_qp *q,bool *counted){(void)q;assert(*counted);*counted=false;}
static bool tbv_qp_mark_error(struct tbv_qp *q){q->state=IB_QPS_ERR;q->errors++;return true;}
static unsigned char memory[64];static bool immediate_ack;
static void ack(struct tbv_send_ctx *s);
static int tbv_native_send_ctx_post_frames(struct tbv_send_ctx *s,int reason){
 assert(reason==TBV_SEND_POST_INITIAL && !s->dead);if(post_error)return post_error;
 assert(!s->tqp->active);s->tqp->active=s;s->posts++;
 /* Within-write rail fragments can arrive in reverse order. */
 for(int i=s->length-1;i>=0;--i)memory[s->offset+i]=s->value;
 if(immediate_ack)ack(s);
 return 0;
}
#include "helpers.c"
static void qp_init(struct tbv_qp *q,struct owner *o){memset(q,0,sizeof(*q));q->backend=TBV_BACKEND_NATIVE;q->type=IB_QPT_RC;q->state=1;q->owner=o;init(&q->pending_sends);assert(!pthread_mutex_init(&q->lock,NULL));}
static void send_init(struct tbv_send_ctx *s,struct tbv_qp *q,int id,int offset,int n,unsigned char v){memset(s,0,sizeof(*s));s->tqp=q;s->id=id;s->refs=1;s->sq_counted=true;s->offset=offset;s->length=n;s->value=v;init(&s->node);}
static void dispatch(struct tbv_qp *q){q->native_sq_work.pending=false;tbv_native_sq_work(&q->native_sq_work.work);}
static void ack(struct tbv_send_ctx *s){
 struct tbv_qp *q=s->tqp;assert(q->active==s);q->active=NULL;struct list_head complete;init(&complete);unsigned long f;
 spin_lock_irqsave(&q->lock,f);s->ready=true;tbv_qp_drain_ready_sends_locked(q,&complete);spin_unlock_irqrestore(&q->lock,f);
 while(!list_empty(&complete)){struct tbv_send_ctx *x=list_first_entry(&complete,struct tbv_send_ctx,node);list_del(&x->node);tbv_send_ctx_put(x);}
}
static bool publish(struct tbv_send_ctx *s){tbv_send_ctx_get(s);return tbv_qp_queue_send(s->tqp,s);}
static void submit(struct tbv_send_ctx *s){bool deferred=publish(s);if(!deferred)assert(!tbv_native_send_ctx_post_frames(s,TBV_SEND_POST_INITIAL));tbv_send_ctx_put(s);}
static void *ack_and_dispatch(void *data){struct tbv_send_ctx *s=data;struct tbv_qp *q=s->tqp;ack(s);if(q->native_sq_work.pending)dispatch(q);return NULL;}
int main(void){
 struct owner owner={0};struct tbv_qp q;qp_init(&q,&owner);struct tbv_send_ctx a,b,c;
 /* Legacy early placement + ordered notifications leaves old final bytes. */
 memset(memory,0,sizeof(memory));memset(memory+8,0x22,16);memset(memory+8,0x11,8);assert(memory[8]!=0x22);
 puts("REPRO: later write placed first, older late fragment overwrites it despite ordered notifications");
 memset(memory,0,sizeof(memory));send_init(&a,&q,0xffffff,0,16,0x11);send_init(&b,&q,0,8,16,0x22);send_init(&c,&q,1,40,8,0x33);
 submit(&a);submit(&b);submit(&c);assert(a.posts==1 && !b.posts && !c.posts && b.native_deferred && c.native_deferred);
 ack(&a);assert(q.native_sq_work.pending && !q.active);dispatch(&q);assert(b.posts==1 && !c.posts);ack(&b);dispatch(&q);ack(&c);
 for(int i=0;i<8;i++)assert(memory[i]==0x11);for(int i=8;i<24;i++)assert(memory[i]==0x22);for(int i=40;i<48;i++)assert(memory[i]==0x33);
 assert(a.dead && b.dead && c.dead && list_empty(&q.pending_sends));
 /* ACK racing the caller return: returned deferred decision is immutable,
  * and caller's pre-publication reference prevents early free/duplicate post. */
 send_init(&a,&q,10,0,8,1);send_init(&b,&q,11,0,8,2);submit(&a);bool deferred=publish(&b);assert(deferred);ack(&a);immediate_ack=true;dispatch(&q);immediate_ack=false;assert(!b.dead && b.refs==1 && b.posts==1);assert(deferred);tbv_send_ctx_put(&b);assert(b.dead);
 /* Empty queue, closing, RNR wait/active head: no duplicate wire start. */
 dispatch(&q);assert(!q.active);send_init(&a,&q,20,0,8,1);send_init(&b,&q,21,0,8,2);submit(&a);submit(&b);dispatch(&q);assert(a.posts==1 && !b.posts);ack(&a);
 post_error=-ENOMEM;dispatch(&q);assert(b.native_deferred && b.refs==1 && q.native_sq_work.pending && q.native_sq_work.delay==1);
 post_error=0;dispatch(&q);ack(&b);
 send_init(&a,&q,30,0,8,1);submit(&a);q.closing=true;ack(&a);assert(!q.native_sq_work.pending);q.closing=false;
 /* Failed prefix must not start later deferred data. */
 send_init(&a,&q,40,0,8,1);send_init(&b,&q,41,0,8,2);submit(&a);submit(&b);a.completion_status=-1;ack(&a);assert(!q.native_sq_work.pending && !b.posts);list_del(&b.node);b.pending=false;tbv_send_ctx_put(&b);
 /* Distinct QPs retain independent wire progress. */
 struct tbv_qp other;qp_init(&other,&owner);send_init(&a,&q,50,0,8,1);send_init(&b,&other,50,32,8,2);submit(&a);submit(&b);assert(q.active==&a && other.active==&b);ack(&a);ack(&b);
 dmabuf=true;assert(!tbv_native_sq_ordered(&q));dmabuf=false;q.type=0;assert(!tbv_native_sq_ordered(&q));q.type=IB_QPT_RC;q.backend=0;assert(!tbv_native_sq_ordered(&q));
 q.backend=TBV_BACKEND_NATIVE;q.type=IB_QPT_RC;q.state=1;
 for(int i=0;i<1000;++i){
  send_init(&a,&q,2*i,0,8,1);send_init(&b,&q,2*i+1,0,8,2);submit(&a);
  immediate_ack=true;pthread_t t;assert(!pthread_create(&t,NULL,ack_and_dispatch,&a));submit(&b);assert(!pthread_join(t,NULL));immediate_ack=false;
  assert(a.dead && b.dead && b.posts==1 && list_empty(&q.pending_sends) && !q.active);
 }
 puts("PASS:1000 concurrent ACK/admission/completion races: exactly one post and no lost ownership");
 puts("PASS: partial overlap/late fragments, PSN wrap, one wire send per QP, independent QPs, ACK-before-return ownership, admission backoff, failed-prefix and closing gates");
}
