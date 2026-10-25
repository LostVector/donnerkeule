/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the real timer functions with concurrent wakeups and timer expiry. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sched.h>
typedef uint64_t u64;
typedef atomic_int atomic_t;
#define atomic_read(p) atomic_load(p)
#define NSEC_PER_USEC 1000
#define HRTIMER_MODE_REL 0
#define ns_to_ktime(n) (n)
#define READ_ONCE(n) (n)
enum hrtimer_restart { HRTIMER_NORESTART, HRTIMER_RESTART };
struct hrtimer { bool enqueued;u64 expires; };
struct state { atomic_t verbs_qps; };
static struct state state;
static struct {
    pthread_mutex_t progress_lock;
    bool progress_running,progress_stopping;
    struct hrtimer progress_timer;
    atomic_t progress_batches;
    struct state *state;
    void *progress_wq;
    int progress_work;
} dmabuf_engine={.progress_lock=PTHREAD_MUTEX_INITIALIZER,.state=&state};
#define spin_lock_irqsave(p,f) do { (f)=0;assert(!pthread_mutex_lock(p)); } while(0)
#define spin_unlock_irqrestore(p,f) do { (void)(f);assert(!pthread_mutex_unlock(p)); } while(0)
static bool dmabuf_verbs=true,dmabuf_poll_control=true;
static unsigned dmabuf_poll_us=25;
static u64 now_ns,starts,ticks,queued,canceled;
static atomic_llong dmabuf_progress_ticks;
#define atomic64_inc(p) atomic_fetch_add(p,1)
static int hrtimer_start(struct hrtimer *t,u64 delta,int mode) {
    (void)mode;t->enqueued=true;t->expires=now_ns+delta;++starts;return 0;
}
static void hrtimer_forward_now(struct hrtimer *t,u64 delta) {
    /* This is the exact condition the real kernel warns about. */
    assert(!t->enqueued);t->expires=now_ns+delta;++ticks;
}
static void hrtimer_cancel(struct hrtimer *t) { t->enqueued=false;++canceled; }
static void cancel_work_sync(int *w) { (void)w; }
static bool queue_work(void *q,int *w) { (void)q;(void)w;++queued;return true; }
#include "timer-active.c"
#include "timer-start-stop.c"
#include "timer-callback.c"
static void fire(void) {
    pthread_mutex_lock(&dmabuf_engine.progress_lock);
    dmabuf_engine.progress_timer.enqueued=false;now_ns+=25000;
    pthread_mutex_unlock(&dmabuf_engine.progress_lock);
    enum hrtimer_restart result=tbv_dmabuf_progress_timer(&dmabuf_engine.progress_timer);
    if(result==HRTIMER_RESTART) {
        pthread_mutex_lock(&dmabuf_engine.progress_lock);
        dmabuf_engine.progress_timer.enqueued=true;
        pthread_mutex_unlock(&dmabuf_engine.progress_lock);
    }
}
static void *wakeups(void *p) {
    (void)p;
    for(int n=0;n<100000;n++)tbv_dmabuf_progress_arm();
    return NULL;
}
int main(void) {
    atomic_store(&state.verbs_qps,1);
    tbv_dmabuf_progress_arm();assert(starts==1);
    u64 original_deadline=dmabuf_engine.progress_timer.expires;
    for(int n=0;n<10000;n++)tbv_dmabuf_progress_start();
    assert(starts==1 && dmabuf_engine.progress_timer.expires==original_deadline);
    pthread_t threads[4];
    for(int n=0;n<4;n++)assert(!pthread_create(&threads[n],NULL,wakeups,NULL));
    for(int n=0;n<100000;n++)fire();
    for(int n=0;n<4;n++)assert(!pthread_join(threads[n],NULL));
    assert(starts==1 && ticks==100000 && queued==100000);
    atomic_store(&state.verbs_qps,0);fire();
    assert(!dmabuf_engine.progress_running && !dmabuf_engine.progress_timer.enqueued);
    atomic_store(&state.verbs_qps,1);tbv_dmabuf_progress_arm();assert(starts==2);
    dmabuf_poll_control=false;atomic_store(&state.verbs_qps,0);fire();
    atomic_store(&dmabuf_engine.progress_batches,1);
    tbv_dmabuf_progress_start();assert(starts==3);fire();assert(dmabuf_engine.progress_running);
    atomic_store(&dmabuf_engine.progress_batches,0);fire();assert(!dmabuf_engine.progress_running);
    dmabuf_poll_control=true;atomic_store(&state.verbs_qps,1);tbv_dmabuf_progress_arm();
    for(int n=0;n<4;n++)assert(!pthread_create(&threads[n],NULL,wakeups,NULL));
    tbv_dmabuf_progress_stop();
    for(int n=0;n<4;n++)assert(!pthread_join(threads[n],NULL));
    u64 stopped_starts=starts;
    for(int n=0;n<10000;n++)tbv_dmabuf_progress_arm();
    assert(starts==stopped_starts && canceled==1 && !dmabuf_engine.progress_running);
    fire();assert(!dmabuf_engine.progress_timer.enqueued);
    dmabuf_engine.progress_stopping=false;dmabuf_poll_us=0;tbv_dmabuf_progress_start();
    assert(starts==stopped_starts);
    puts("PASS concurrent callback/wakeup ownership, no postponed deadline, idle rearm, batch-only progress, and stop races");
}
