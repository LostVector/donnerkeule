/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TBV_PERF_TYPES_H
#define TBV_PERF_TYPES_H

#include <linux/types.h>

/* No payload, addresses or keys. Token is scoped to the source host's boot;
 * QPs/PSN identify the WR, offset the batch, rail the local physical rail.
 * wr_id is available at the verbs boundary, not in the on-wire protocol. */
struct tbv_perf_id {
	u64 token;
	u64 wr_id;
	u32 src_qp, dst_qp, psn;
	u32 offset, bytes, rail;
	u8 tx, opcode;
};

#endif
