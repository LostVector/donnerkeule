/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef TBV_GPU_POOL_H
#define TBV_GPU_POOL_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
typedef u32 tbv_gpu_u32;
typedef u64 tbv_gpu_u64;
#else
#include <errno.h>
#include <stdint.h>
typedef uint32_t tbv_gpu_u32;
typedef uint64_t tbv_gpu_u64;
#endif

#define TBV_GPU_FRAME_BYTES 4096u
#define TBV_GPU_MAX_RAILS 8u
#define TBV_GPU_MAX_FRAMES_PER_RAIL 512u
#define TBV_GPU_MAX_POOL_BYTES (256ull << 20)

/* Same contiguous block division as native WRITE striping, in whole frames. */
static inline tbv_gpu_u32 tbv_gpu_block_start(tbv_gpu_u32 frames,
					      tbv_gpu_u32 rails,
					      tbv_gpu_u32 rail)
{
	return ((tbv_gpu_u64)frames * rail + rails - 1) / rails;
}

static inline int tbv_gpu_check_transfer(tbv_gpu_u64 pool_bytes,
					 tbv_gpu_u64 offset,
					 tbv_gpu_u64 bytes,
					 tbv_gpu_u32 rails)
{
	if (!rails || rails > TBV_GPU_MAX_RAILS || !bytes ||
	    (offset | bytes) % TBV_GPU_FRAME_BYTES ||
	    offset > pool_bytes || bytes > pool_bytes - offset)
		return -EINVAL;
	if (bytes / TBV_GPU_FRAME_BYTES >
	    (tbv_gpu_u64)rails * TBV_GPU_MAX_FRAMES_PER_RAIL)
		return -EMSGSIZE;
	return 0;
}

/* A descriptor may not straddle mapped SG entries, even adjacent IOVAs. */
static inline int tbv_gpu_segment_frame(tbv_gpu_u64 address,
					tbv_gpu_u64 length,
					tbv_gpu_u64 offset,
					tbv_gpu_u64 *out)
{
	if (offset > length || TBV_GPU_FRAME_BYTES > length - offset ||
	    address > ~(tbv_gpu_u64)0 - offset ||
	    address + offset > ~(tbv_gpu_u64)0 - (TBV_GPU_FRAME_BYTES - 1) ||
	    (address + offset) % TBV_GPU_FRAME_BYTES)
		return -EINVAL;
	*out = address + offset;
	return 0;
}

#endif
