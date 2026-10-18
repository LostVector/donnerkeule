/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TBV_PERF_H
#define TBV_PERF_H

#include <linux/timekeeping.h>
#include "perf_types.h"
#include "../proto/verbs_dmabuf.h"
#ifndef TBV_DISABLE_PROFILING
#include "trace.h"
#endif

/* The tracepoint static key gates clocks and records. Enabling tracing during
 * a transfer yields a partial trace; only complete, bounded runs qualify. */
static inline u64 tbv_perf_clock(void)
{
#ifndef TBV_DISABLE_PROFILING
	if (trace_tbv_perf_enabled())
		return ktime_get_ns();
#endif
	return 0;
}

static inline void tbv_perf_interval(const struct tbv_perf_id *id,
				     const char *phase, u64 start, u64 end,
				     int status, u32 aux)
{
#ifndef TBV_DISABLE_PROFILING
	if (start && end >= start)
		trace_tbv_perf(id, phase, start, end, status, aux);
#endif
}

static inline void tbv_perf_span(const struct tbv_perf_id *id, const char *phase,
				u64 start, int status, u32 aux)
{
	if (start)
		tbv_perf_interval(id, phase, start, tbv_perf_clock(), status, aux);
}

static inline void tbv_perf_mark(const struct tbv_perf_id *id, const char *phase,
				int status, u32 aux)
{
	u64 now = tbv_perf_clock();

	tbv_perf_interval(id, phase, now, now, status, aux);
}

static inline struct tbv_perf_id tbv_perf_wire_id(const struct tbv_dmabuf_wire *m,
						bool tx)
{
	return (struct tbv_perf_id) {
		.token = m->token, .src_qp = m->hdr.src_qp,
		.dst_qp = m->hdr.dest_qp, .psn = m->hdr.psn,
		.offset = m->offset, .bytes = m->bytes,
		.tx = tx, .opcode = m->hdr.opcode,
	};
}

static inline void tbv_perf_wire(const struct tbv_dmabuf_wire *m, bool tx,
				 const char *phase, u64 start, int status, u32 aux)
{
	if (start) {
		struct tbv_perf_id id = tbv_perf_wire_id(m, tx);

		tbv_perf_span(&id, phase, start, status, aux);
	}
}

static inline void tbv_perf_wire_mark(const struct tbv_dmabuf_wire *m, bool tx,
				      const char *phase, int status, u32 aux)
{
	u64 now = tbv_perf_clock();

	if (now) {
		struct tbv_perf_id id = tbv_perf_wire_id(m, tx);

		tbv_perf_interval(&id, phase, now, now, status, aux);
	}
}

#endif
