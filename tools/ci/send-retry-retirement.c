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
static void tbv_send_retire_frames(struct tbv_send_ctx*s,u32 n,int status){(void)status;s->retired+=n;}
static bool __attribute__((unused)) tbv_send_is_completed(struct tbv_send_ctx*s){return s->completed;}
static void tbv_send_ctx_put(struct tbv_send_ctx*s){s->puts++;}
static bool tbv_native_sq_ordered(struct tbv_qp*q){(void)q;return true;}
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
int main(void){
 struct tbv_qp q={0};struct tbv_send_ctx s={.tqp=&q,.completed=true};active=&s;scenario=1;
 tbv_send_tx_done(&s,-ECANCELED);
 if(expect(q.errors==0&&s.retired==1&&s.puts==1&&!ordered_calls,"already-completed cancellation poisoned QP or lost retirement"))return 1;
 q.errors=0;s=(struct tbv_send_ctx){.tqp=&q};scenario=1;ordered_calls=0;
 tbv_send_tx_done(&s,-EIO);
 if(expect(q.errors==0&&s.retired==1&&s.puts==1&&ordered_calls==1,"ACK retirement race poisoned QP"))return 1;
 q.errors=0;s=(struct tbv_send_ctx){.tqp=&q};scenario=2;ordered_calls=0;
 tbv_send_tx_done(&s,-EIO);
 if(expect(q.errors==0&&s.retired==1&&s.puts==1&&ordered_calls==1,"ACK-ready success poisoned QP"))return 1;
 q.errors=0;s=(struct tbv_send_ctx){.tqp=&q};scenario=0;ordered_calls=0;
 tbv_send_tx_done(&s,-EIO);
 if(expect(q.errors==1&&s.completed&&s.retired==1&&s.puts==2&&ordered_calls==1,"genuine pending failure did not fail QP and retire refs"))return 1;
 q.errors=0;s=(struct tbv_send_ctx){.tqp=&q};ordered_calls=0;
 tbv_send_tx_done(&s,0);
 if(expect(!q.errors&&s.retired==1&&s.puts==1&&!ordered_calls,"ordinary TX completion changed"))return 1;
 puts("Actual send callback: completed cancel/ACK races safe; genuine errors and reference retirement preserved");return 0;
}
