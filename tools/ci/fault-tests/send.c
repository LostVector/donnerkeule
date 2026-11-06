// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&n,&n}
#define list_empty(h) ((h)->next==(h))
#define list_first_entry(h,t,m) ((t *)((char *)((h)->next)-offsetof(t,m)))
static void list_del_init(struct list_head*n){n->prev->next=n->next;n->next->prev=n->prev;n->next=n->prev=n;}
static void list_add(struct list_head*n,struct list_head*h){n->next=h;n->prev=h->prev;h->prev->next=n;h->prev=n;}
struct tbv_qp {int errors;};
struct tbv_send_ctx {struct tbv_qp *tqp;u32 psn;struct list_head node;bool completed;int completion_status,retired,puts;};
static struct tbv_send_ctx *active;
static int scenario,ordered_calls;
static bool ordered = true;
static void tbv_send_retire_frames(struct tbv_send_ctx*s,u32 n,int status){(void)status;s->retired+=n;}
static bool __attribute__((unused)) tbv_send_is_completed(struct tbv_send_ctx*s){return s->completed;}
static void tbv_send_ctx_put(struct tbv_send_ctx*s){s->puts++;}
static bool tbv_native_sq_ordered(struct tbv_qp*q){(void)q;return ordered;}
static void tbv_qp_mark_error(struct tbv_qp*q){q->errors++;}
static bool tbv_qp_complete_send_ordered(struct tbv_qp*q,u32 psn,int status,struct list_head*done,struct tbv_send_ctx**matched){
 (void)q;(void)psn;(void)matched;ordered_calls++;
 if(scenario==1){active->completed=true;return false;} /* ACK retired it during lookup. */
 if(scenario==2){active->completion_status=0;return true;} /* ACK ready behind earlier send. */
 active->completion_status=status;list_add(&active->node,done);return true;
}
static bool tbv_send_complete(struct tbv_send_ctx*s,int status){s->completed=true;s->completion_status=status;return true;}
#include "send-helper.c"
static int expect(bool b,const char*m){if(!b){fprintf(stderr,"FAIL %s\n",m);return 1;}return 0;}

int main(void) {
 unsigned count=0;
 const int statuses[]={0,-ECANCELED,-EIO,-ENOMEM,-ETIMEDOUT};
 for(unsigned complete=0;complete<2;complete++)
 for(unsigned lookup=0;lookup<3;lookup++)
 for(unsigned order=0;order<2;order++)
 for(unsigned status=0;status<sizeof(statuses)/sizeof(statuses[0]);status++) {
  struct tbv_qp q={0};struct tbv_send_ctx s={.tqp=&q,.completed=complete};
  active=&s;scenario=lookup;ordered=order;ordered_calls=0;
  const bool enters=statuses[status]!=0&&!complete;
  const bool owns_failure=enters&&lookup==0;
  tbv_send_tx_done(&s,statuses[status]);
  if(expect(q.errors==(owns_failure&&order),"independent QP error oracle"))return 1;
  if(expect(s.retired==1&&s.puts==1+owns_failure,"DMA retirement and exactly held references"))return 1;
  if(expect(ordered_calls==enters,"completed or successful callback must not re-complete"))return 1;
  count++;
 }
 printf("PASS TX: %u completion/ACK/cancel/failure state combinations\n",count);
 return 0;
}
