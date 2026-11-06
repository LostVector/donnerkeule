// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "proto/native_data.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef long long atomic64_t;
#define atomic64_inc(p) (++*(p))
#define atomic64_add(n,p) (*(p)+=(n))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define pr_warn_ratelimited(...) ((void)0)
#define DMA_FROM_DEVICE 0
#define TBV_DATA_FRAME_SIZE 4096u
#define TBV_DATA_CREDIT_CONTROL_RESERVE 256u
#define TBV_BACKEND_NATIVE 0
#define TBV_BACKEND_APPLE 1
#define TBV_PATH_RING_STARTED 1
#define TBV_PATH_TUNNEL_ENABLED 2
/* Public Linux7.3 ring descriptor error flag values. */
#define RING_DESC_CRC_ERROR 1u
#define RING_DESC_BUFFER_OVERRUN 4u
struct ring_frame {u32 size,flags,sof,eof;};
struct tb_ring {int unused;};
struct tbv_state {
 bool verbs_registered;
 atomic64_t data_rx_canceled,data_rx_completed,data_rx_bad_frame,data_rx_crc_error,data_rx_overrun;
 atomic64_t data_rx_raw_poisoned,data_rx_raw_poison_drop,data_rx_bad_header,data_rx_bad_header_len;
 atomic64_t data_rx_bad_header_path_credit,data_rx_repost_failed,data_rx_lost;
};
struct peer {int backend;};
struct tbv_rail {struct peer *peer;struct {u32 path_id;}key;};
struct tbv_path {
 struct tbv_rail *rail;bool rx_raw_pending,rx_raw_poisoned,rx_data_seq_valid;
 u16 rx_data_seq_next;int state;
 struct {u32 rx_ring_size;}cfg;
 atomic64_t data_rx_canceled,data_rx_completed,rx_bytes,data_rx_repost_failed,data_rx_lost;
};
struct tbv_data_frame {struct ring_frame frame;struct tbv_path *path;u8 *buf;u64 dma;};
static struct tbv_state state;
static unsigned forwarded,raw_calls,apple_calls,reposts,credits,remote_credits;
static int repost_error;
static bool has_state = true;
static struct tbv_state *tbv_path_state(struct tbv_path *p){(void)p;return has_state ? &state : NULL;}
static void *tb_ring_dma_device(struct tb_ring *r){return r;}
static void dma_sync_single_for_cpu(void *d,u64 dma,u32 n,int dir){(void)d;(void)dma;(void)n;(void)dir;}
static void tbv_ibdev_rx_native_frame(struct tbv_state *s,struct tbv_path *p,const struct tbv_native_data_header *h,const u8 *b){(void)s;(void)p;(void)h;(void)b;forwarded++;}
static void tbv_ibdev_rx_apple_frame(struct tbv_state *s,struct tbv_path *p,const void*b,u32 n,u32 a,u32 z){(void)s;(void)p;(void)b;(void)n;(void)a;(void)z;apple_calls++;}
static void tbv_path_rx_raw_payload(struct tbv_path*p,struct tbv_state*s,const void*b,u32 n){(void)p;(void)s;(void)b;(void)n;raw_calls++;}
static void tbv_path_rx_start_raw(struct tbv_path*p,const struct tbv_native_data_header*h){(void)h;p->rx_raw_pending=true;}
static int tbv_path_post_rx_frame(struct tbv_data_frame*f){(void)f;reposts++;return repost_error;}
static void tbv_path_return_rx_data_credit(struct tbv_path*p,u32 n){(void)p;credits+=n;}
static void tbv_path_add_remote_rx_credits(struct tbv_path*p,u32 n){(void)p;remote_credits+=n;}
static void tbv_rx_bad_header_parse_note(struct tbv_state*s,struct tbv_path*p,const void*b,u32 n,int ret){(void)p;(void)b;(void)n;(void)ret;s->data_rx_bad_header++;}
static void tbv_rx_bad_header_note(struct tbv_state*s,struct tbv_path*p,atomic64_t*c,const char*why,const struct tbv_native_data_header*h,u32 n,int ret){(void)p;(void)why;(void)h;(void)n;(void)ret;s->data_rx_bad_header++;(*c)++;}
void tbv_ibdev_rx_frame(struct tbv_state *, struct tbv_path *, const void *, u32);
#include "helpers.c"
static struct peer peer={.backend=TBV_BACKEND_NATIVE};
static struct tbv_rail rail={.peer=&peer,.key={.path_id=257}};
static struct tbv_path path;
static struct tbv_data_frame frame;
static struct tb_ring ring;
static u8 buffer[4096];
static void reset(void){
 memset(&state,0,sizeof(state));state.verbs_registered=true;has_state=true;peer.backend=TBV_BACKEND_NATIVE;memset(buffer,0,sizeof(buffer));
 memset(&path,0,sizeof(path));path.rail=&rail;path.state=TBV_PATH_TUNNEL_ENABLED;path.cfg.rx_ring_size=1024;
 memset(&frame,0,sizeof(frame));frame.path=&path;frame.buf=buffer;frame.frame.size=4096;frame.frame.flags=0xa;
 forwarded=raw_calls=apple_calls=reposts=credits=remote_credits=0;repost_error=0;
}
static void packet(u16 seq){
 struct tbv_native_data_header h={.opcode=TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM,.length=4048,.flags=TBV_NATIVE_DATA_F_LAST};
 assert(tbv_native_data_build_header(buffer,sizeof(buffer),&h)>=0);
 tbv_wire_put_le16(buffer+TBV_NATIVE_DATA_PATH_SEQ_OFFSET,seq|TBV_NATIVE_DATA_PATH_SEQ_PRESENT);
}
static void deliver(void){tbv_path_rx_complete(&ring,&frame.frame,false);}

/* A table of external events, independent of the implementation's branch order. */
static unsigned long cases;
static void event_matrix(void) {
 const u32 lengths[]={0,1,47,48,64,2816,3072,3840,4096,4097};
 for(unsigned flags=0;flags<16;flags++)
 for(unsigned n=0;n<sizeof(lengths)/sizeof(lengths[0]);n++)
 for(unsigned canceled=0;canceled<2;canceled++)
 for(unsigned attached=0;attached<2;attached++)
 for(unsigned registered=0;registered<2;registered++)
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++)
 for(unsigned raw=0;raw<2;raw++)
 for(unsigned fail_post=0;fail_post<2;fail_post++) {
  reset();packet(4);has_state=attached;state.verbs_registered=registered;
  path.state=lifecycle;path.rx_raw_pending=raw;repost_error=fail_post?-1:0;
  frame.frame.flags=flags;frame.frame.size=lengths[n];
  const bool corrupt=(flags&5)!=0, open=lifecycle!=0;
  const unsigned effective=lengths[n]?lengths[n]:4096;
  tbv_path_rx_complete(&ring,&frame.frame,canceled);
  assert(reposts==(!canceled&&open));
  if(canceled||corrupt||!attached||effective>4096) {
   assert(!forwarded&&!raw_calls&&!apple_calls&&!credits&&!remote_credits);
  } else if(raw) {
   assert(raw_calls==1&&!forwarded);
  } else {
   assert(forwarded==(registered&&effective==4096));
   assert(!raw_calls);
  }
  assert(path.rx_raw_poisoned==(!canceled&&corrupt&&raw));
  assert(state.data_rx_crc_error==(!canceled&&attached&&((flags&1)!=0)));
  assert(state.data_rx_overrun==(!canceled&&attached&&((flags&4)!=0)));
  if(fail_post||!open||canceled)assert(!credits&&!remote_credits);
  cases++;
 }
}

static uint32_t random_state=0x63a8d471;
static uint32_t draw(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void sequences(void) {
 /* Independent oracle uses an absolute sender index; only the wire wraps.
  * Lost/corrupt frames must return credits once a later valid frame arrives. */
 for(unsigned run=0;run<128;run++) {
  reset();uint32_t sender=32750, last=0;bool seen=false;unsigned expected=0;
  for(unsigned i=0;i<1024;i++) {
   const bool corrupt=(draw()%7)==0;
   sender+=1+draw()%3;
   packet(sender&32767);frame.frame.flags=corrupt?0xb:0xa;deliver();
   if(!corrupt){expected+=seen?sender-last:1;last=sender;seen=true;}
   assert(credits==expected);assert(forwarded==state.data_rx_completed-state.data_rx_crc_error);
   cases++;
  }
 }
 reset();packet(12);deliver();unsigned before=credits;
 packet(5000);deliver();assert(credits==before+1); /* restart, not 4987 credits */
 packet(5000);deliver();assert(credits==before+2); /* duplicate is not a huge gap */
 packet(5001);tbv_wire_put_le16(buffer+TBV_NATIVE_DATA_PATH_SEQ_OFFSET,0);deliver();
 assert(credits==before+3&&path.rx_data_seq_next==5001);
 has_state=false;packet(5003);assert(tbv_path_rx_note_seq(&path,buffer)==2);
}

static void control_and_raw(void) {
 for(unsigned field=0;field<10;field++) {
  reset();frame.frame.size=48;
  struct tbv_native_data_header h={.opcode=TBV_NATIVE_DATA_OP_PATH_CREDIT,.imm_data=7};
  switch(field){case 1:h.imm_data=0;break;case 2:h.flags=1;break;case 3:h.dest_qp=1;break;case 4:h.src_qp=1;break;case 5:h.psn=1;break;case 6:h.length=1;break;case 7:h.remote_addr=1;break;case 8:h.rkey=1;break;case 9:h.opcode=TBV_NATIVE_DATA_OP_SEND_ACK;break;}
  assert(tbv_native_data_build_header(buffer,sizeof(buffer),&h)>=0);deliver();
  assert(remote_credits==(field==0?7:0));
 }
 const u8 bad_flags[]={TBV_NATIVE_DATA_F_RAW_STREAM|TBV_NATIVE_DATA_F_BLOCK,TBV_NATIVE_DATA_F_RAW_STREAM};
 for(unsigned i=0;i<4;i++) {
  reset();frame.frame.size=i==0?64:48;
  struct tbv_native_data_header h={.opcode=TBV_NATIVE_DATA_OP_RDMA_WRITE,.flags=TBV_NATIVE_DATA_F_RAW_STREAM,.length=i==1?0:17};
  if(i==2)h.flags=bad_flags[0];
  assert(tbv_native_data_build_header(buffer,sizeof(buffer),&h)>=0);deliver();
  assert(path.rx_raw_pending==(i==3));
 }
 reset();path.rx_raw_pending=true;frame.frame.flags=0xb;deliver();
 frame.frame.flags=0xb;deliver();assert(state.data_rx_raw_poisoned==1);
 frame.frame.flags=0xa;deliver();assert(!raw_calls&&!forwarded&&state.data_rx_raw_poison_drop==1);
 has_state=false;deliver();assert(!raw_calls&&!forwarded);
 reset();peer.backend=TBV_BACKEND_APPLE;deliver();assert(apple_calls==1);
 reset();path.rail=NULL;packet(0);deliver();assert(forwarded==1);
 reset();rail.peer=NULL;packet(0);deliver();assert(forwarded==1);rail.peer=&peer;
 for(unsigned ring_size=0;ring_size<=1024;ring_size++) {
  unsigned expected=ring_size>256?ring_size-256:ring_size/2;
  if(expected>32)expected-=expected%32;
  if(ring_size&&!expected)expected=1;
  assert(tbv_path_data_credit_window(ring_size)==expected);
 }
 const u8 data_ops[]={TBV_NATIVE_DATA_OP_SEND,TBV_NATIVE_DATA_OP_SEND_IMM,TBV_NATIVE_DATA_OP_RDMA_WRITE,TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM,TBV_NATIVE_DATA_OP_RDMA_READ_REQ,TBV_NATIVE_DATA_OP_RDMA_READ_RESP,TBV_NATIVE_DATA_OP_MAD};
 for(unsigned op=0;op<256;op++) {
  bool expected=false;
  for(unsigned i=0;i<sizeof(data_ops);i++)if(data_ops[i]==op)expected=true;
  assert(tbv_native_data_consumes_rx_credit(op)==expected);
 }
 reset();packet(0);tbv_ibdev_rx_frame(NULL,&path,buffer,4096);assert(!forwarded);
 state.verbs_registered=false;tbv_ibdev_rx_frame(&state,&path,buffer,4096);assert(!forwarded);
 struct tbv_native_data_header wrong_opcode={.opcode=TBV_NATIVE_DATA_OP_SEND_ACK,.imm_data=7};
 assert(!tbv_native_data_valid_path_credit(&wrong_opcode));
 reset();has_state=false;packet(1);assert(!tbv_path_rx_note_seq(&path,buffer));
 reset();packet(1);path.rx_raw_poisoned=true;deliver();assert(!forwarded&&!raw_calls);
 reset();packet(1);path.rx_raw_poisoned=path.rx_raw_pending=true;frame.frame.flags=0xb;deliver();
 assert(path.rx_raw_poisoned&&!state.data_rx_raw_poisoned&&!forwarded&&!raw_calls);
}

int main(void) {
 event_matrix();sequences();control_and_raw();
 printf("PASS RX: %lu deterministic event/oracle cases, seed=0x63a8d471\n",cases);
 return 0;
}
