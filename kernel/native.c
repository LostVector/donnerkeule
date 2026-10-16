// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>

#include "transport.h"

const struct tbv_backend_ops tbv_native_backend_ops = {
	.type = TBV_BACKEND_NATIVE,
	.name = "native-linux",
	.supports_rc = true,
	.supports_uc = true,
	.needs_tbnet_identity = false,
};

static int tbv_native_validate_config(const struct tbv_cfg_link *link)
{
	if (link->backend != TBV_CFG_BACKEND_NATIVE)
		return -EINVAL;
	if (!link->app_selection.valid)
		return -EINVAL;
	if (link->app_selection.gid_type != TBV_ID_GID_ROCE_V2)
		return -EINVAL;

	return 0;
}

const struct tbv_transport_ops tbv_native_transport_ops = {
	.type = TBV_BACKEND_NATIVE,
	.name = "native-linux",
	.validate_config = tbv_native_validate_config,
};
