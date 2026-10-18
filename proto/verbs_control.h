/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef TBV_VERBS_CONTROL_WIRE_H
#define TBV_VERBS_CONTROL_WIRE_H

#include "verbs_dmabuf.h"

#define TBV_CTRL_FRAME_SIZE 256u
#define TBV_CTRL_FRAME_MAGIC 0x31434254u /* TBC1 */
#define TBV_CTRL_SETUP_SIZE 128u
#define TBV_CTRL_SETUP_MAGIC 0x31534254u /* TBS1 */
#define TBV_CTRL_VERSION 1u
#define TBV_CTRL_SETUP 16u
#define TBV_CTRL_READY 17u
#define TBV_CTRL_RING_SIZE 128u

struct tbv_ctrl_setup {
	tbv_wire_u16 op;
	int status;
	tbv_wire_u32 path;
	tbv_wire_u8 src_host[16];
	tbv_wire_u8 dst_host[16];
	tbv_wire_u8 domain[16];
};

static inline int tbv_ctrl_setup_build(void *buf, size_t size,
				     const struct tbv_ctrl_setup *m, tbv_wire_u64 route)
{
	tbv_wire_u8 *p = buf;
	tbv_wire_u16 op = m->op & ~TBV_DMABUF_REPLY;

	if (!p || size < TBV_CTRL_SETUP_SIZE ||
	    (op != TBV_CTRL_SETUP && op != TBV_CTRL_READY) ||
	    m->path < 8 || m->path > 0xffff)
		return -EINVAL;
	memset(p, 0, TBV_CTRL_SETUP_SIZE);
	tbv_native_wire_put_xdomain_header(p, route, 0, m->op, TBV_CTRL_SETUP_SIZE);
	tbv_wire_put_le32(p + 32, TBV_CTRL_SETUP_MAGIC);
	tbv_wire_put_le16(p + 36, TBV_CTRL_VERSION);
	tbv_wire_put_le16(p + 38, m->op);
	tbv_wire_put_le32(p + 40, m->status);
	tbv_wire_put_le32(p + 44, m->path);
	memcpy(p + 48, m->src_host, 16);
	memcpy(p + 64, m->dst_host, 16);
	memcpy(p + 80, m->domain, 16);
	tbv_wire_put_le32(p + 96, TBV_CTRL_RING_SIZE);
	return TBV_CTRL_SETUP_SIZE;
}

static inline int tbv_ctrl_setup_parse(const void *buf, size_t size,
				     struct tbv_ctrl_setup *m)
{
	const tbv_wire_u8 *p = buf;
	tbv_wire_u16 op;
	unsigned int i;

	if (!p || !m || size != TBV_CTRL_SETUP_SIZE ||
	    memcmp(p + 12, tbv_native_wire_uuid, 16) ||
	    (tbv_wire_get_le32(p + 8) & 0x3fu) != (TBV_CTRL_SETUP_SIZE - 12) / 4 ||
	    tbv_wire_get_le32(p + 32) != TBV_CTRL_SETUP_MAGIC ||
	    tbv_wire_get_le16(p + 36) != TBV_CTRL_VERSION ||
	    tbv_wire_get_le32(p + 96) != TBV_CTRL_RING_SIZE)
		return -EINVAL;
	op = tbv_wire_get_le16(p + 38);
	if (((op & ~TBV_DMABUF_REPLY) != TBV_CTRL_SETUP &&
	     (op & ~TBV_DMABUF_REPLY) != TBV_CTRL_READY) ||
	    tbv_wire_get_le32(p + 28) != op)
		return -EINVAL;
	for (i = 100; i < TBV_CTRL_SETUP_SIZE; i += 4)
		if (tbv_wire_get_le32(p + i))
			return -EINVAL;
	memset(m, 0, sizeof(*m));
	m->op = op;
	m->status = (int)tbv_wire_get_le32(p + 40);
	m->path = tbv_wire_get_le32(p + 44);
	memcpy(m->src_host, p + 48, 16);
	memcpy(m->dst_host, p + 64, 16);
	memcpy(m->domain, p + 80, 16);
	return m->path >= 8 && m->path <= 0xffff ? 0 : -EINVAL;
}

static inline int tbv_ctrl_frame_build(void *buf, size_t size,
				     const struct tbv_dmabuf_wire *m)
{
	tbv_wire_u8 *p = buf;
	int ret;

	if (!p || size < TBV_CTRL_FRAME_SIZE)
		return -EINVAL;
	ret = tbv_dmabuf_wire_build(p + 4, TBV_DMABUF_WIRE_SIZE, m, 0);
	if (ret < 0)
		return ret;
	tbv_wire_put_le32(p, TBV_CTRL_FRAME_MAGIC);
	return TBV_CTRL_FRAME_SIZE;
}

static inline int tbv_ctrl_frame_parse(const void *buf, size_t size,
				     struct tbv_dmabuf_wire *m)
{
	const tbv_wire_u8 *p = buf;

	if (!p || size != TBV_CTRL_FRAME_SIZE ||
	    tbv_wire_get_le32(p) != TBV_CTRL_FRAME_MAGIC)
		return -EINVAL;
	return tbv_dmabuf_wire_parse(p + 4, TBV_DMABUF_WIRE_SIZE, m);
}

#endif
