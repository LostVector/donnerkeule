/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef TBV_GPU_DIRECT_UAPI_H
#define TBV_GPU_DIRECT_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define TBV_GPU_ABI_VERSION 1u
#define TBV_GPU_UAPI_MAX_RAILS 8u
#define TBV_GPU_TX 1u
#define TBV_GPU_RX 2u

struct tbv_gpu_rail_id {
	__u32 peer;
	__u32 rail;
};

struct tbv_gpu_rail_info {
	struct tbv_gpu_rail_id id;
	__u32 local_path;
	__u32 remote_path;
	__u8 local_domain[16];
	__u8 remote_domain[16];
	__u8 local_host[16];
	__u8 remote_host[16];
};

struct tbv_gpu_topology {
	__u32 version;
	__u32 count;
	struct tbv_gpu_rail_info rails[TBV_GPU_UAPI_MAX_RAILS];
};

/* Dedicated, distinct TX/RX BOs. No descriptors are posted by IMPORT. */
struct tbv_gpu_import {
	__u32 version;
	__u32 count;
	__s32 tx_fd;
	__s32 rx_fd;
	__aligned_u64 bytes;
	__aligned_u64 reserved;
	struct tbv_gpu_rail_id rails[TBV_GPU_UAPI_MAX_RAILS];
};

/* PREPARE_RX leases RX until WAIT_RX, then until GPU consumption finishes.
 * SUBMIT_TX requires a GPU system release and a peer PREPARE_RX handshake.
 * Sequence starts at 1 and increases by exactly 1 per direction. */
struct tbv_gpu_transfer {
	__aligned_u64 sequence;
	__aligned_u64 offset;
	__aligned_u64 bytes;
};

/* WAIT never releases RX back to hardware. Only the next PREPARE_RX does.
 * Any completion error, timeout or interrupted wait permanently aborts the
 * session. Close drains all descriptors before unmapping either BO. */
struct tbv_gpu_wait {
	__u32 direction;
	__u32 timeout_ms;
	__aligned_u64 sequence;
	__aligned_u64 completed_bytes;
	__s32 status;
	__u32 reserved;
};

#define TBV_GPU_GET_TOPOLOGY _IOR('G', 0x80, struct tbv_gpu_topology)
#define TBV_GPU_IMPORT _IOW('G', 0x81, struct tbv_gpu_import)
#define TBV_GPU_PREPARE_RX _IOW('G', 0x82, struct tbv_gpu_transfer)
#define TBV_GPU_SUBMIT_TX _IOW('G', 0x83, struct tbv_gpu_transfer)
#define TBV_GPU_WAIT _IOWR('G', 0x84, struct tbv_gpu_wait)
#define TBV_GPU_ABORT _IO('G', 0x85)

#endif
