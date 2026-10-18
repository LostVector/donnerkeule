/* SPDX-License-Identifier: GPL-2.0 */
/* Shim for state tests. Real clock/static-key behavior is tested separately. */
#include <stdint.h>
typedef uint64_t u64;
struct tbv_perf_id { uint64_t token, wr_id; uint32_t src_qp, dst_qp, psn, offset, bytes, rail;
                     uint8_t tx, opcode; };
#define tbv_perf_clock() ((u64)0)
#define tbv_perf_span(id, phase, start, status, aux) \
    do { (void)(id); (void)(phase); (void)(start); (void)(status); (void)(aux); } while (0)
#define tbv_perf_interval(id, phase, start, end, status, aux) \
    do { tbv_perf_span(id, phase, start, status, aux); (void)(end); } while (0)
#define tbv_perf_mark(id, phase, status, aux) tbv_perf_span(id, phase, 0, status, aux)
#define tbv_perf_wire(m, tx, phase, start, status, aux) \
    do { tbv_perf_span(m, phase, start, status, aux); (void)(tx); } while (0)
#define tbv_perf_wire_mark(m, tx, phase, status, aux) tbv_perf_wire(m, tx, phase, 0, status, aux)
