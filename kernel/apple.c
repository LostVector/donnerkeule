// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>

#include "transport.h"

const struct tbv_backend_ops tbv_apple_backend_ops = {
	.type = TBV_BACKEND_APPLE,
	.name = "apple-fa57",
	.supports_rc = false,
	.supports_uc = true,
	.needs_tbnet_identity = true,
};

static int tbv_apple_validate_config(const struct tbv_cfg_link *link)
{
	if (link->backend != TBV_CFG_BACKEND_APPLE)
		return -EINVAL;
	if (!link->app_selection.valid)
		return -EINVAL;

	return 0;
}

const struct tbv_transport_ops tbv_apple_transport_ops = {
	.type = TBV_BACKEND_APPLE,
	.name = "apple-fa57",
	.validate_config = tbv_apple_validate_config,
};
