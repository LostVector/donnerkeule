/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef TBV_VERBS_DMABUF_WIRE_H
#define TBV_VERBS_DMABUF_WIRE_H

#include "native_data.h"

/* One 256-byte config frame reserves four bytes for the ctl-layer CRC. */
#define TBV_DMABUF_WIRE_SIZE 252u
#define TBV_DMABUF_WIRE_MAGIC 0x31424454u /* TDB1 */
#define TBV_DMABUF_WIRE_VERSION 1u
#define TBV_DMABUF_MAX_RAILS 4u
#define TBV_DMABUF_BATCH_FRAMES 512u
#define TBV_DMABUF_FRAME_BYTES 4096u
#define TBV_DMABUF_REPLY 0x8000u

enum tbv_dmabuf_op {
	TBV_DMABUF_PREPARE = 5,
	TBV_DMABUF_STATUS = 6,
	TBV_DMABUF_ABORT = 7,
	TBV_DMABUF_COMPLETE = 8,
	TBV_DMABUF_PING = 9,
};

enum tbv_dmabuf_status {
	TBV_DMABUF_READY = 0,
	TBV_DMABUF_BUSY = 1,
	TBV_DMABUF_DONE = 2,
	TBV_DMABUF_RNR = 3,
};

struct tbv_dmabuf_wire_lane {
	tbv_wire_u8 domain[16];
	tbv_wire_u32 path;
};

struct tbv_dmabuf_wire {
	tbv_wire_u16 op;
	int status;
	tbv_wire_u64 token;
	tbv_wire_u8 src_host[16];
	tbv_wire_u8 dst_host[16];
	tbv_wire_u8 src_domain[16];
	struct tbv_native_data_header hdr;
	tbv_wire_u32 offset;
	tbv_wire_u32 bytes;
	tbv_wire_u32 count;
	struct tbv_dmabuf_wire_lane lanes[TBV_DMABUF_MAX_RAILS];
};

/* Identify notifications even when their body fails validation. XDomain's
 * core response matcher checks route/UUID, not our opcode/token, so a bad or
 * stale DONE must never satisfy a concurrent PREPARE or HELLO request. */
static inline bool tbv_dmabuf_wire_notification(const void *buf, size_t size)
{
	const tbv_wire_u8 *p = buf;

	return p && size >= TBV_NATIVE_WIRE_XDOMAIN_HDR_SIZE &&
		!memcmp(p + 12, tbv_native_wire_uuid, 16) &&
		((tbv_wire_get_le32(p + 28) & ~TBV_DMABUF_REPLY) == TBV_DMABUF_COMPLETE ||
		 (size >= 40 &&
		  (tbv_wire_get_le16(p + 38) & ~TBV_DMABUF_REPLY) == TBV_DMABUF_COMPLETE));
}

static inline bool tbv_dmabuf_opcode(tbv_wire_u8 op)
{
	return op == TBV_NATIVE_DATA_OP_SEND ||
	       op == TBV_NATIVE_DATA_OP_SEND_IMM ||
	       op == TBV_NATIVE_DATA_OP_RDMA_WRITE ||
	       op == TBV_NATIVE_DATA_OP_RDMA_WRITE_IMM;
}

static inline int tbv_dmabuf_shape(const struct tbv_dmabuf_wire *m)
{
	tbv_wire_u32 frames;

	if (!m->token || !m->count || m->count > TBV_DMABUF_MAX_RAILS ||
	    !tbv_dmabuf_opcode(m->hdr.opcode) ||
	    m->hdr.psn > 0xffffffu || !m->hdr.src_qp || !m->hdr.dest_qp ||
	    m->hdr.flags & ~(TBV_NATIVE_DATA_F_RAW_STREAM |
			     TBV_NATIVE_DATA_F_SOLICITED) ||
	    m->hdr.length > TBV_NATIVE_DATA_MAX_MSG_SIZE ||
	    m->offset > m->hdr.length ||
	    m->bytes > m->hdr.length - m->offset ||
	    m->offset % TBV_DMABUF_FRAME_BYTES ||
	    (m->offset + m->bytes != m->hdr.length &&
	     m->bytes % TBV_DMABUF_FRAME_BYTES) ||
	    (!m->bytes && m->hdr.length))
		return -EINVAL;
	frames = (m->bytes + TBV_DMABUF_FRAME_BYTES - 1) /
		 TBV_DMABUF_FRAME_BYTES;
	if (frames > m->count * TBV_DMABUF_BATCH_FRAMES)
		return -EMSGSIZE;
	return 0;
}

static inline int tbv_dmabuf_wire_build(void *buf, size_t size,
				      const struct tbv_dmabuf_wire *m,
				      tbv_wire_u64 route)
{
	tbv_wire_u8 *p = buf;
	tbv_wire_u16 op = m->op & ~TBV_DMABUF_REPLY;
	unsigned int i;
	int ret;

	if (!buf || size < TBV_DMABUF_WIRE_SIZE ||
	    op < TBV_DMABUF_PREPARE || op > TBV_DMABUF_PING)
		return -EINVAL;
	ret = tbv_dmabuf_shape(m);
	if (ret)
		return ret;
	memset(p, 0, TBV_DMABUF_WIRE_SIZE);
	tbv_native_wire_put_xdomain_header(p, route, m->token & 3u, m->op,
					    TBV_DMABUF_WIRE_SIZE);
	tbv_wire_put_le32(p + 32, TBV_DMABUF_WIRE_MAGIC);
	tbv_wire_put_le16(p + 36, TBV_DMABUF_WIRE_VERSION);
	tbv_wire_put_le16(p + 38, m->op);
	tbv_wire_put_le32(p + 44, m->status);
	tbv_wire_put_le64(p + 48, m->token);
	memcpy(p + 56, m->src_host, 16);
	memcpy(p + 72, m->dst_host, 16);
	memcpy(p + 88, m->src_domain, 16);
	ret = tbv_native_data_build_header(p + 104, 48, &m->hdr);
	if (ret < 0)
		return ret;
	tbv_wire_put_le32(p + 152, m->offset);
	tbv_wire_put_le32(p + 156, m->bytes);
	tbv_wire_put_le32(p + 160, m->count);
	for (i = 0; i < m->count; i++) {
		memcpy(p + 168 + i * 20, m->lanes[i].domain, 16);
		tbv_wire_put_le32(p + 184 + i * 20, m->lanes[i].path);
	}
	return TBV_DMABUF_WIRE_SIZE;
}

static inline int tbv_dmabuf_wire_parse(const void *buf, size_t size,
				      struct tbv_dmabuf_wire *m)
{
	const tbv_wire_u8 *p = buf;
	tbv_wire_u16 op;
	unsigned int i;
	int ret;

	if (!p || !m || size != TBV_DMABUF_WIRE_SIZE ||
	    memcmp(p + 12, tbv_native_wire_uuid, 16) ||
	    (tbv_wire_get_le32(p + 8) & 0x3fu) !=
		(TBV_DMABUF_WIRE_SIZE - 12) / 4 ||
	    tbv_wire_get_le32(p + 32) != TBV_DMABUF_WIRE_MAGIC ||
	    tbv_wire_get_le16(p + 36) != TBV_DMABUF_WIRE_VERSION ||
	    tbv_wire_get_le32(p + 40) || tbv_wire_get_le32(p + 164) ||
	    tbv_wire_get_le32(p + 248))
		return -EINVAL;
	op = tbv_wire_get_le16(p + 38);
	if ((op & ~TBV_DMABUF_REPLY) < TBV_DMABUF_PREPARE ||
	    (op & ~TBV_DMABUF_REPLY) > TBV_DMABUF_PING ||
	    tbv_wire_get_le32(p + 28) != op)
		return -EINVAL;
	memset(m, 0, sizeof(*m));
	m->op = op;
	m->status = (int)tbv_wire_get_le32(p + 44);
	m->token = tbv_wire_get_le64(p + 48);
	memcpy(m->src_host, p + 56, 16);
	memcpy(m->dst_host, p + 72, 16);
	memcpy(m->src_domain, p + 88, 16);
	ret = tbv_native_data_parse_header(p + 104, 48, &m->hdr);
	if (ret || !(m->hdr.flags & TBV_NATIVE_DATA_F_RAW_STREAM) ||
	    m->hdr.frag_offset)
		return -EINVAL;
	m->offset = tbv_wire_get_le32(p + 152);
	m->bytes = tbv_wire_get_le32(p + 156);
	m->count = tbv_wire_get_le32(p + 160);
	if (m->count > TBV_DMABUF_MAX_RAILS)
		return -EINVAL;
	for (i = 0; i < TBV_DMABUF_MAX_RAILS; i++) {
		memcpy(m->lanes[i].domain, p + 168 + i * 20, 16);
		m->lanes[i].path = tbv_wire_get_le32(p + 184 + i * 20);
		if (i >= m->count) {
			tbv_wire_u8 zero[16] = {0};

			if (m->lanes[i].path || memcmp(m->lanes[i].domain, zero, 16))
				return -EINVAL;
		}
	}
	return tbv_dmabuf_shape(m);
}

/* Replies retain the original source/target orientation. Match the entire
 * transaction, including boot generations, domain, QPs, PSN and batch. */
static inline bool tbv_dmabuf_wire_same(const struct tbv_dmabuf_wire *a,
				      const struct tbv_dmabuf_wire *b, bool batch)
{
	return a->token == b->token && !memcmp(a->src_host, b->src_host, 16) &&
		!memcmp(a->dst_host, b->dst_host, 16) &&
		!memcmp(a->src_domain, b->src_domain, 16) &&
		!memcmp(&a->hdr, &b->hdr, sizeof(a->hdr)) &&
		a->count == b->count &&
		!memcmp(a->lanes, b->lanes, sizeof(a->lanes)) &&
		(!batch || (a->offset == b->offset && a->bytes == b->bytes));
}

#endif
