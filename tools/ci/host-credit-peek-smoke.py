#!/usr/bin/env python3
"""Exercise actual host/GPU credit eligibility and receive reference ownership."""
from smoke_support import ROOT, functions, run_c
actual=functions((ROOT/'kernel/ibdev.c').read_text(), ('tbv_ibdev_dmabuf_peek',))
shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint64_t u64;typedef uint32_t u32;
#define TBV_NATIVE_DATA_OP_SEND 1
#define TBV_NATIVE_DATA_OP_SEND_IMM 2
#define IB_QPT_RC 1
#define TBV_RX_ENDPOINT_OK 0
#define IB_ACCESS_LOCAL_WRITE 1
#define GFP_KERNEL 0
#define SZ_4K 4096ull
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define check_add_overflow(a,b,p) __builtin_add_overflow((a),(b),(p))
#define mutex_lock(x) ((void)(x))
#define mutex_unlock(x) ((void)(x))
#define spin_lock_irqsave(x,f) ((void)(x),(f)=0)
#define spin_unlock_irqrestore(x,f) ((void)(x),(void)(f))
struct tbv_state {int unused;};struct pd {int unused;};
struct peer {char remote_host_id[16];};struct tbv_rail {struct peer *peer;};
struct tbv_native_data_header {u32 opcode,dest_qp,psn,length;};
struct wqe {u64 addr,length;u32 lkey;};
struct tbv_mr {struct {struct pd *pd;} base;void *dmabuf;bool dma_mr,eligible;u32 access;u64 virt_addr,start,length;};
struct tbv_qp {struct {struct pd *pd;} base;int rx_lock,lock,type;u32 rx_expected_psn,recv_count,recv_head,recv_popped;bool closing;struct wqe recvq[1];struct tbv_rail *rail;};
struct tbv_dmabuf_recv_ctx {struct tbv_qp *tqp;struct wqe wqe;u32 recv_popped;struct tbv_native_data_header hdr;bool peeked;struct tbv_mr *mr;u64 address;};
struct tbv_dmabuf_buffer {void *mr,*host_mr,*owner,*context;u64 offset;int (*copy)(void*,u32,void*,u32,bool);bool (*alive)(void*);int (*finish)(void*,int);};
static struct pd pd;static struct peer peer;static struct tbv_rail rail={&peer};
static struct tbv_qp qp;static struct tbv_mr mr;static unsigned qprefs,mrrefs,allocations;
static bool dmabuf_host_credits,lookup=true,allocation_fail,posting=true;static int endpoint;
static struct tbv_qp *tbv_qp_get_by_num(struct tbv_state *s,u32 n){(void)s;(void)n;if(!lookup)return NULL;qprefs++;return &qp;}
static void tbv_qp_put(struct tbv_qp *p){assert(p==&qp&&qprefs);qprefs--;}
static bool tbv_qp_allows_post(struct tbv_qp *p){(void)p;return posting;}
static int tbv_qp_validate_native_endpoint(struct tbv_qp *p,struct tbv_native_data_header *h){(void)p;(void)h;return endpoint;}
static void *kzalloc(size_t n,int flags){(void)flags;if(allocation_fail)return NULL;allocations++;return calloc(1,n);}
static void kfree(void *p){if(p){assert(allocations);allocations--;free(p);}}
static struct tbv_mr *tbv_mr_get(struct tbv_state *s,u32 key){(void)s;if(key!=7)return NULL;mrrefs++;return &mr;}
static void tbv_mr_put(struct tbv_mr *p){if(p){assert(mrrefs);mrrefs--;}}
static bool tbv_host_dma_eligible(struct tbv_mr *p,u64 o,u64 n){assert(p==&mr);return mr.eligible&&!(o%4096)&&n>=65536&&!(n%4096)&&o<=mr.length&&n<=mr.length-o;}
static int tbv_dmabuf_recv_copy(void *c,u32 o,void *d,u32 n,bool b){(void)c;(void)o;(void)d;(void)n;(void)b;return 0;}
static bool tbv_dmabuf_recv_alive(void *c){(void)c;return true;}
static int tbv_dmabuf_recv_finish(void *c,int e){(void)c;return e;}
'''
tests = r'''
static void reset(void){
 assert(!qprefs&&!mrrefs&&!allocations);memset(&qp,0,sizeof(qp));memset(&mr,0,sizeof(mr));
 qp.base.pd=&pd;qp.type=IB_QPT_RC;qp.rx_expected_psn=123;qp.recv_count=1;qp.rail=&rail;
 mr.base.pd=&pd;mr.access=IB_ACCESS_LOCAL_WRITE;mr.start=0x10000000;mr.virt_addr=0x20000000;mr.length=2097152;mr.eligible=true;
 qp.recvq[0]=(struct wqe){mr.start,2097152,7};dmabuf_host_credits=true;lookup=true;endpoint=0;allocation_fail=false;posting=true;
}
static void invoke(bool good,int expected){
 struct tbv_native_data_header h={TBV_NATIVE_DATA_OP_SEND,1,0,65536};struct tbv_dmabuf_buffer b={0};
 int rc=tbv_ibdev_dmabuf_peek(NULL,&rail,&h,&b);assert(rc==expected);
 if(good){assert(b.owner==&qp&&b.context&&h.psn==123&&qprefs==1&&mrrefs==1&&allocations==1);assert((mr.dmabuf&&b.mr==mr.dmabuf&&!b.host_mr)||(!mr.dmabuf&&b.host_mr==&mr&&!b.mr));assert(qp.recv_count==1);kfree(b.context);tbv_mr_put(&mr);tbv_qp_put(&qp);}
 assert(!qprefs&&!mrrefs&&!allocations);
}
int main(void){
 reset();invoke(true,0);
 reset();dmabuf_host_credits=false;invoke(false,-EACCES);
 reset();mr.dmabuf=(void*)1;dmabuf_host_credits=false;invoke(true,0);
 reset();mr.dma_mr=true;invoke(false,-EACCES);
 reset();mr.base.pd=NULL;invoke(false,-EACCES);
 reset();mr.access=0;invoke(false,-EACCES);
 reset();mr.eligible=false;invoke(false,-EACCES);
 reset();qp.recvq[0].length=32768;invoke(false,-EACCES);
 reset();qp.recvq[0].addr+=32;invoke(false,-EACCES);
 reset();qp.recvq[0].addr-=4096;invoke(false,-EACCES);
 reset();qp.recvq[0].addr+=mr.length;invoke(false,-EACCES);
 reset();qp.recvq[0].lkey=8;invoke(false,-EACCES);
 reset();qp.recv_count=0;invoke(false,-EAGAIN);
 reset();qp.closing=true;invoke(false,-EAGAIN);
 reset();endpoint=1;invoke(false,-EACCES);
 reset();lookup=false;invoke(false,-ENOENT);
 reset();allocation_fail=true;invoke(false,-ENOMEM);
 reset();posting=false;invoke(false,-EACCES);
 reset();qp.type=2;invoke(false,-EACCES);
 reset();mr.virt_addr=UINT64_MAX-1023;invoke(false,-EACCES);
 reset();mr.virt_addr=UINT64_MAX-131071;invoke(false,-EACCES);
 reset();struct peer wrong={.remote_host_id={1}};struct tbv_rail other={&wrong};qp.rail=&other;invoke(false,-EACCES);
 return 0;
}
'''
run_c("host-credit-peek",shim+actual+tests)
print("Host credit peek: PASS (opt-in host/GPU, PD/access, bounds, invalid key, closing/empty QP, endpoint and reference cleanup)")
