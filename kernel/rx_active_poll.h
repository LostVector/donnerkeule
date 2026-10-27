/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TBV_RX_ACTIVE_POLL_H
#define TBV_RX_ACTIVE_POLL_H
#ifndef __KERNEL__
#include <stdbool.h>
#endif
/* Pure deadline gate shared by production and bounded host checks. */
static inline bool tbv_rx_active_poll_again(unsigned int us, unsigned int frames,
                                           unsigned int budget, bool enabled,
                                           unsigned long long start,
                                           unsigned long long now)
{
 return enabled && frames && frames < budget && us && us <= 1000 &&
        now - start < (unsigned long long)us * 1000;
}
#endif
