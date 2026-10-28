// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define READ_ONCE(x) (x)
#define IB_QPS_ERR 6
#define time_after_eq(a,b) ((long)((a)-(b))>=0)
#define spin_lock_irqsave(p,f) do{(f)=0;assert(!pthread_mutex_lock(p));}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);assert(!pthread_mutex_unlock(p));}while(0)
struct tbv_qp {pthread_mutex_t lock;bool closing;int state;unsigned timers;};
struct tbv_send_ctx {struct tbv_qp *tqp;_Atomic int tx_pending;bool retryable,retrying,pending,rnr_waiting,completed;unsigned long queued_jiffies;};
struct tbv_path {int unused;};
struct tbv_send_build_job {struct tbv_send_ctx *send;struct tbv_path *path;u32 nfrags,first_frag;};
static unsigned long jiffies=1000;
static u32 unsent;
static void atomic_sub(unsigned n,_Atomic int *p){int old=atomic_fetch_sub(p,(int)n);assert(old>=(int)n);}
static bool atomic_sub_and_test(unsigned n,_Atomic int *p){int old=atomic_fetch_sub(p,(int)n);assert(old>=(int)n);return old==(int)n;}
static void tbv_send_mark_queued(struct tbv_send_ctx *s,unsigned long now){s->queued_jiffies=now;}
static void tbv_qp_schedule_timeout_locked(struct tbv_qp *q){if(!q->closing && q->state!=IB_QPS_ERR)q->timers++;}
static u32 tbv_native_send_build_block(struct tbv_send_ctx *s,struct tbv_path *p,u32 first,u32 n){(void)s;(void)p;(void)first;assert(unsent<=n);return unsent;}
#include "helpers.c"
static void reset(struct tbv_send_ctx *s,int n){s->tx_pending=n;s->pending=true;s->retryable=true;s->retrying=true;s->completed=false;s->rnr_waiting=false;s->queued_jiffies=0;s->tqp->timers=0;}
struct arg {struct tbv_send_ctx *s;u32 n;};
static void *retire(void *a){struct arg *x=a;tbv_send_retire_frames(x->s,x->n,0);return NULL;}
int main(void){
 struct tbv_qp qp={0};assert(!pthread_mutex_init(&qp.lock,NULL));struct tbv_send_ctx s={.tqp=&qp};struct tbv_send_build_job job={.send=&s,.nfrags=5};
 reset(&s,5);unsent=5;old_run_build_job(&job);
 assert(s.tx_pending==0 && s.queued_jiffies==0 && qp.timers==0);
 assert(!tbv_qp_entry_expired(s.queued_jiffies,100000,67));
 puts("REPRO: original abandoned build retires all frames but never arms retry; queued=0 cannot expire");
 reset(&s,5);tbv_native_send_run_build_job(&job);
 assert(s.tx_pending==0 && s.queued_jiffies==jiffies && qp.timers==1 && !s.retrying);
 assert(!tbv_qp_entry_expired(s.queued_jiffies,jiffies+66,67));
 assert(tbv_qp_entry_expired(s.queued_jiffies,jiffies+67,67));
 /* Actual completions before the abandoned-frame retirement. */
 reset(&s,5);unsent=3;tbv_send_retire_frames(&s,2,0);tbv_native_send_run_build_job(&job);assert(!s.tx_pending && qp.timers==1);
 /* Abandoned frames first, then actual completion. */
 reset(&s,5);tbv_native_send_run_build_job(&job);assert(s.tx_pending==2 && !qp.timers);tbv_send_retire_frames(&s,2,0);assert(!s.tx_pending && qp.timers==1);
 reset(&s,3);s.pending=false;tbv_send_retire_frames(&s,3,0);assert(!qp.timers);
 reset(&s,3);s.rnr_waiting=true;tbv_send_retire_frames(&s,3,0);assert(!qp.timers);
 reset(&s,3);tbv_send_retire_frames(&s,3,-1);assert(!qp.timers);
 reset(&s,3);s.retryable=false;tbv_send_retire_frames(&s,3,0);assert(s.tx_pending==3 && !qp.timers);
 for(int i=0;i<1000;i++){
  reset(&s,7);struct arg a={&s,3},b={&s,4};pthread_t ta,tb;
  assert(!pthread_create(&ta,NULL,retire,&a));assert(!pthread_create(&tb,NULL,retire,&b));
  assert(!pthread_join(ta,NULL));assert(!pthread_join(tb,NULL));assert(!s.tx_pending && qp.timers==1 && !s.retrying);
 }
 puts("PASS: abandoned build arms retry; both completion orders; RNR/error/closing guards;1000 concurrent final-retirement races arm exactly once");
}
