#!/usr/bin/env python3
"""Check the production DMA wait body with simulated completion/timeout races.

This covers watchdog decisions, not Linux wait-queue concurrency or DMA.
"""
import os
import shlex
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
source = (ROOT / "kernel/verbs_dmabuf.c").read_text()
matches = re.findall(
    r"^static int tbv_dmabuf_batch_wait\([\s\S]*?^\}", source, re.MULTILINE)
if len(matches) != 1:
    raise RuntimeError("Expected exactly one production DMA wait function")

shim = r"""
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
typedef int atomic_t;
#define atomic_read(p) (*(p))
#define msecs_to_jiffies(ms) (ms)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
static unsigned long jiffies;
struct tbv_dmabuf_batch;
struct wait_queue { struct tbv_dmabuf_batch *batch; };
struct tbv_dmabuf_batch {
    atomic_t pending, error;
    struct wait_queue wait;
};
struct tbv_dmabuf_buffer { bool (*alive)(void *); void *context; };
struct outcome { long remaining; int pending, error; };
static struct outcome outcomes[3];
static unsigned int waits, kicks, outcome_count, complete_on_kick;
static long simulated_wait(struct wait_queue *queue, unsigned long timeout) {
    assert(timeout == 10 && waits < outcome_count);
    struct outcome next = outcomes[waits++];
    jiffies += timeout - next.remaining;
    queue->batch->pending = next.pending;
    queue->batch->error = next.error;
    return next.remaining;
}
#define wait_event_timeout(queue, condition, timeout) \
    ((condition) ? (timeout) : simulated_wait(&(queue), (timeout)))
static void tbv_dmabuf_batch_kick(struct tbv_dmabuf_batch *batch, bool tx) {
    assert(tx && batch->pending && !batch->error);
    kicks++;
    if (kicks == complete_on_kick)
        batch->pending = 0;
}
static bool alive(void *context) { return *(bool *)context; }
"""

cases = r"""
static struct tbv_dmabuf_batch batch;
static bool connected;
static struct tbv_dmabuf_buffer buffer = { alive, &connected };
static void reset(void) {
    batch = (struct tbv_dmabuf_batch){ .pending = 1 };
    batch.wait.batch = &batch;
    connected = true;
    jiffies = waits = kicks = outcome_count = complete_on_kick = 0;
}
static void add(long remaining, int pending, int error) {
    assert(outcome_count < 3);
    outcomes[outcome_count++] = (struct outcome){ remaining, pending, error };
}
static void check(int result, unsigned int expected_waits,
                  unsigned int expected_kicks) {
    assert(tbv_dmabuf_batch_wait(&batch, &buffer, 100) == result);
    assert(waits == expected_waits && kicks == expected_kicks);
}
int main(void) {
    reset(); batch.pending = 0; check(0, 0, 0);
    reset(); batch.error = -EIO; check(-EIO, 0, 0);
    reset(); add(9, 0, 0); check(0, 1, 0); /* Completion woke the waiter. */
    reset(); add(9, 1, -EIO); check(-EIO, 1, 0);
    reset(); add(0, 1, 0); complete_on_kick = 1; check(0, 1, 1);
    reset(); add(0, 1, 0); add(0, 1, 0);
    complete_on_kick = 2; check(0, 2, 2); /* Watchdog remains repeatable. */
    reset(); add(0, 0, 0); check(0, 1, 0); /* Completion at timeout. */
    reset(); add(0, 1, -EIO); check(-EIO, 1, 0); /* Error at timeout. */
    reset(); add(1, 1, 0); add(9, 0, 0); check(0, 2, 0);
    reset(); connected = false; check(-ECANCELED, 0, 0);
    reset(); jiffies = 100; check(-ETIMEDOUT, 0, 0);
    puts("Verbs DMA wait: PASS (completion, error, timeout recovery/races, cancellation)");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="tbv-dma-wait-") as directory:
    path = pathlib.Path(directory) / "smoke.c"
    binary = pathlib.Path(directory) / "smoke"
    path.write_text(shim + matches[0] + cases)
    subprocess.run([
        *shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", str(path), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
