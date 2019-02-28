// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <asm/io.h>

#define REG_WDT_CLR			0x0
#define REG_WDT_DUMMY_REG_1	0x4
#define REG_WDT_RST_RSTLEN	0x8
#define REG_WDT_INTR_PERIOD 0xC
#define REG_WDT_MAX_PRD_L	0x10
#define REG_WDT_MAX_PRD_H	0x14

struct msc313e_wdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
};

static int msc313e_wdt_start(struct watchdog_device *wdev){
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);
	iowrite16(0xFFFF, priv->base + REG_WDT_MAX_PRD_L);
	iowrite16(0xFFFF, priv->base + REG_WDT_MAX_PRD_H);
	iowrite16(1, priv->base + REG_WDT_CLR);
	return 0;
}

static int msc313e_wdt_ping(struct watchdog_device *wdev){
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);
	iowrite16(0xFFFF, priv->base + REG_WDT_MAX_PRD_L);
	iowrite16(0xFFFF, priv->base + REG_WDT_MAX_PRD_H);
	return 0;
}

static int msc313e_wdt_stop(struct watchdog_device *wdev){
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);
	iowrite16(0, priv->base + REG_WDT_CLR);
	return 0;
}

static int msc313e_wdt_restart(struct watchdog_device *wdev, unsigned long x, void *y){
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);
	dev_info(wdev->parent, "triggering reset via WDT, hold onto your pants..");
	iowrite16(0x00FF, priv->base + REG_WDT_MAX_PRD_L);
	iowrite16(0x0000, priv->base + REG_WDT_MAX_PRD_H);
	iowrite16(1, priv->base + REG_WDT_CLR);
	return 0;
}

static const struct watchdog_info msc313e_wdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "MSC313e WDT",
};

static const struct watchdog_ops msc313e_wdt_ops = {
	.owner = THIS_MODULE,
	.start = msc313e_wdt_start,
	.stop = msc313e_wdt_stop,
	.ping = msc313e_wdt_ping,
	.restart = msc313e_wdt_restart,
};

static const struct of_device_id msc313e_wdt_of_match[] = {
	{
		.compatible = "mstar,msc313e-wdt",
	},
	{}
};
MODULE_DEVICE_TABLE(of, msc313e_wdt_of_match);

static int msc313e_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	const struct of_device_id *id;
	struct msc313e_wdt_priv *priv;
	struct resource *mem;

	id = of_match_node(msc313e_wdt_of_match, pdev->dev.of_node);
		if (!id)
			return -ENODEV;

		priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if(!priv)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->wdev.info = &msc313e_wdt_ident,
	priv->wdev.ops = &msc313e_wdt_ops,
	priv->wdev.parent = &pdev->dev;
	priv->wdev.min_timeout = 1;
	priv->wdev.max_timeout = 350;
	priv->wdev.timeout = 30;

	platform_set_drvdata(pdev, priv);
	watchdog_set_drvdata(&priv->wdev, priv);

	ret = watchdog_register_device(&priv->wdev);

	return ret;
}

static int msc313e_wdt_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_wdt_driver = {
	.driver = {
		.name = "msc313e-wdt",
		.of_match_table = msc313e_wdt_of_match,
	},
	.probe = msc313e_wdt_probe,
	.remove = msc313e_wdt_remove,
};
module_platform_driver(msc313e_wdt_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313e WDT driver");
