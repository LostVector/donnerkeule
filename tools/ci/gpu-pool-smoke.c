/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include "proto/gpu_pool.h"

int main(void)
{
	tbv_gpu_u64 address;
	unsigned int rails, frames, rail;

	/* Exhaust geometry, including empty blocks and non-divisible counts. */
	for (rails = 1; rails <= TBV_GPU_MAX_RAILS; rails++) {
		for (frames = 1; frames <= rails * TBV_GPU_MAX_FRAMES_PER_RAIL; frames++) {
			unsigned int covered = 0;
			for (rail = 0; rail < rails; rail++) {
				unsigned int first = tbv_gpu_block_start(frames, rails, rail);
				unsigned int end = tbv_gpu_block_start(frames, rails, rail + 1);
				assert(first == covered);
				assert(end >= first && end - first <= TBV_GPU_MAX_FRAMES_PER_RAIL);
				covered = end;
			}
			assert(covered == frames);
			assert(tbv_gpu_check_transfer(TBV_GPU_MAX_POOL_BYTES, 4096,
				(tbv_gpu_u64)frames * 4096, rails) == 0);
		}
	}
	assert(tbv_gpu_check_transfer(8192, 4096, 4096, 4) == 0);
	assert(tbv_gpu_check_transfer(8192, 4096, 8192, 4) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, UINT64_MAX - 4095, 4096, 4) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, 1, 4096, 4) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, 0, 4095, 4) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, 0, 0, 4) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, 0, 4096, 0) == -EINVAL);
	assert(tbv_gpu_check_transfer(8192, 0, 4096, 9) == -EINVAL);
	assert(tbv_gpu_check_transfer(TBV_GPU_MAX_POOL_BYTES, 0, 513 * 4096, 1) == -EMSGSIZE);
	assert(tbv_gpu_segment_frame(0x10000, 0x10000, 0xf000, &address) == 0);
	assert(address == 0x1f000);
	assert(tbv_gpu_segment_frame(0x10000, 0x10000, 0x10000, &address) == -EINVAL);
	assert(tbv_gpu_segment_frame(0x10001, 4096, 0, &address) == -EINVAL);
	assert(tbv_gpu_segment_frame(0x10000, 8191, 4096, &address) == -EINVAL);
	assert(tbv_gpu_segment_frame(UINT64_MAX - 4095, 8192, 4096, &address) == -EINVAL);
	puts("GPU pool geometry: PASS (all 1..8 rail divisions, bounds, SG boundaries, overflow)");
	return 0;
}
