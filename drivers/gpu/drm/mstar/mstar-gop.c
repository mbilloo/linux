/* SPDX-License-Identifier: GPL-2.0-or-later */
#define DRIVER_NAME "mstar-gop"

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include "mstar-gop.h"

static int mstar_gop_probe(struct platform_device *pdev)
{
	return 0;
}

static int mstar_gop_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mstar_gop_dt_ids[] = {
	{ .compatible = "mstar,gop" },
	{},
};
MODULE_DEVICE_TABLE(of, mstar_gop_dt_ids);

static struct platform_driver mstar_gop_driver = {
	.probe = mstar_gop_probe,
	.remove = mstar_gop_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = mstar_gop_dt_ids,
	},
};

module_platform_driver(mstar_gop_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mstar GOP Driver");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
