/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thunderbolt_ibverbs

#if !defined(_TBV_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TBV_TRACE_H

#include <linux/tracepoint.h>
#include "perf_types.h"

#ifndef TBV_DISABLE_PROFILING
TRACE_EVENT(tbv_perf,
	TP_PROTO(const struct tbv_perf_id *id, const char *phase,
		 u64 start_ns, u64 end_ns, int status, u32 aux),
	TP_ARGS(id, phase, start_ns, end_ns, status, aux),
	TP_STRUCT__entry(
		__string(phase, phase)
		__field(u64, token)
		__field(u64, wr_id)
		__field(u32, src_qp)
		__field(u32, dst_qp)
		__field(u32, psn)
		__field(u32, offset)
		__field(u32, bytes)
		__field(u32, rail)
		__field(u8, tx)
		__field(u8, opcode)
		__field(u64, start_ns)
		__field(u64, end_ns)
		__field(int, status)
		__field(u32, aux)
	),
	TP_fast_assign(
		__assign_str(phase);
		__entry->token = id->token;
		__entry->wr_id = id->wr_id;
		__entry->src_qp = id->src_qp;
		__entry->dst_qp = id->dst_qp;
		__entry->psn = id->psn;
		__entry->offset = id->offset;
		__entry->bytes = id->bytes;
		__entry->rail = id->rail;
		__entry->tx = id->tx;
		__entry->opcode = id->opcode;
		__entry->start_ns = start_ns;
		__entry->end_ns = end_ns;
		__entry->status = status;
		__entry->aux = aux;
	),
	TP_printk("phase=%s token=%llu wr_id=%llu src_qp=%u dst_qp=%u psn=%u offset=%u bytes=%u rail=%u tx=%u opcode=%u start_ns=%llu end_ns=%llu status=%d aux=%u",
		__get_str(phase), __entry->token, __entry->wr_id,
		__entry->src_qp, __entry->dst_qp, __entry->psn,
		__entry->offset, __entry->bytes, __entry->rail, __entry->tx,
		__entry->opcode, __entry->start_ns, __entry->end_ns,
		__entry->status, __entry->aux)
);
#endif

TRACE_EVENT(tbv_cfgfs_link_op,
	TP_PROTO(const char *name, const char *op, int ret),

	TP_ARGS(name, op, ret),

	TP_STRUCT__entry(
		__string(name, name)
		__string(op, op)
		__field(int, ret)
	),

	TP_fast_assign(
		__assign_str(name);
		__assign_str(op);
		__entry->ret = ret;
	),

	TP_printk("name=%s op=%s ret=%d",
		  __get_str(name), __get_str(op), __entry->ret)
);

TRACE_EVENT(tbv_active_link,
	TP_PROTO(const char *name, u32 link_id, const char *backend,
		 bool active, u32 device_id, u8 port, u8 gid_index,
		 u8 gid_type),

	TP_ARGS(name, link_id, backend, active, device_id, port, gid_index,
		gid_type),

	TP_STRUCT__entry(
		__string(name, name)
		__field(u32, link_id)
		__string(backend, backend)
		__field(bool, active)
		__field(u32, device_id)
		__field(u8, port)
		__field(u8, gid_index)
		__field(u8, gid_type)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->link_id = link_id;
		__assign_str(backend);
		__entry->active = active;
		__entry->device_id = device_id;
		__entry->port = port;
		__entry->gid_index = gid_index;
		__entry->gid_type = gid_type;
	),

	TP_printk("name=%s link=%u backend=%s active=%u dev=%u port=%u gid=%u gid_type=%u",
		  __get_str(name), __entry->link_id, __get_str(backend),
		  __entry->active, __entry->device_id, __entry->port,
		  __entry->gid_index, __entry->gid_type)
);

#endif /* _TBV_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
