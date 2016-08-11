/*
 * Watchdog driver for Renesas WDT watchdog
 *
 * Copyright (C) 2015-16 Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 * Copyright (C) 2015-16 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/watchdog.h>

#define RWDT_DEFAULT_TIMEOUT 60U

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rwdt_data {
	const unsigned int regwidth;
	const int cntoffset;
	const int tcoffset;
	const int rstoffset;

	const int wtitbit;
	const int tmebit;
	const int wovfbit;
	const int wrfbit;
	const int rstebit;

	const unsigned int *clk_divs;
	const int numdivs;
	const u32 countermax;
};

struct rwdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
	struct clk *clk;
	unsigned int clks_per_sec;
	u8 cks;
	const struct rwdt_data *data;
};

static void rwdt_write(struct rwdt_priv *priv, u32 val, unsigned int reg)
{
	bool counter = reg == priv->data->cntoffset;
	switch(priv->data->regwidth){
	case 4:
		if (counter)
			val |= 0x5a5a0000;
		else
			val |= 0xa5a5a500;

		writel(val, priv->base + reg);
		break;
	case 2:
		if (counter || (reg == priv->data->rstoffset && (val & BIT(priv->data->rstebit))) )
			val |= 0x5a00;
		else
			val |= 0xa500;
		writew(val, priv->base + reg);
		break;
	}
}

static int rwdt_init_timeout(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	rwdt_write(priv, priv->data->countermax - wdev->timeout * priv->clks_per_sec, priv->data->cntoffset);

	return 0;
}

static int rwdt_start(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	clk_prepare_enable(priv->clk);

	rwdt_write(priv, priv->cks, priv->data->tcoffset);
	rwdt_init_timeout(wdev);

	if(priv->data->wrfbit >= 0){
		while (readb_relaxed(priv->base + priv->data->tcoffset) & BIT(priv->data->wrfbit))
			cpu_relax();
	}

	if(priv->data->rstebit >= 0){
		rwdt_write(priv, BIT(priv->data->rstebit), priv->data->rstoffset);
	}

	rwdt_write(priv, priv->cks | BIT(priv->data->tmebit), priv->data->tcoffset);

	return 0;
}

static int rwdt_stop(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	rwdt_write(priv, priv->cks, priv->data->tcoffset);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static unsigned int rwdt_get_timeleft(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);
	u16 val = readw_relaxed(priv->base + priv->data->cntoffset);

	return DIV_ROUND_CLOSEST(priv->data->countermax - val, priv->clks_per_sec);
}

static const struct watchdog_info rwdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "Renesas WDT Watchdog",
};

static int rwdt_restart(struct watchdog_device *wdev, unsigned long x, void *y){
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);
	if(priv->data->rstebit >= 0){
		rwdt_write(priv, 0, priv->data->tcoffset);
		readb(priv->base + priv->data->rstoffset);
		rwdt_write(priv, 0, priv->data->rstoffset);

		rwdt_write(priv, priv->data->countermax, priv->data->cntoffset);
		rwdt_write(priv, BIT(priv->data->rstebit), priv->data->rstoffset);
		rwdt_write(priv, (BIT(priv->data->wtitbit) | BIT(priv->data->tmebit)), priv->data->tcoffset);
	}
	return 0;
}

static const struct watchdog_ops rwdt_ops = {
	.owner = THIS_MODULE,
	.start = rwdt_start,
	.stop = rwdt_stop,
	.ping = rwdt_init_timeout,
	.get_timeleft = rwdt_get_timeleft,
	.restart = rwdt_restart,
};

/*
 * This driver does also fit for R-Car Gen2 (r8a779[0-4]) WDT. However, for SMP
 * to work there, one also needs a RESET (RST) driver which does not exist yet
 * due to HW issues. This needs to be solved before adding compatibles here.
 */

static const unsigned int rcar_gen3_divs[] = { 1, 4, 16, 32, 64, 128, 1024 };
static const unsigned int r7s72100_divs[] = { 1, 4, 16, 32, 64, 128, 1024, 4096, 16384 };

static const struct rwdt_data rwdt_data_rcar_gen3 = {
		.regwidth = 4, .cntoffset = 0, .tcoffset = 4, .wtitbit = 6, .tmebit = 7, .wovfbit = 5, .wrfbit = 4, .rstebit = -1,
		.clk_divs = rcar_gen3_divs, .numdivs = ARRAY_SIZE(rcar_gen3_divs), .countermax = 65536,
};

static const struct rwdt_data rwdt_data_r7s72100 = {
		.regwidth = 2, .cntoffset = 2, .tcoffset = 0, .rstoffset = 4, .wtitbit = 6, .tmebit = 5, .wovfbit = 7, .wrfbit = -1, .rstebit = 6,
		.clk_divs = r7s72100_divs, .numdivs = ARRAY_SIZE(r7s72100_divs), .countermax = 255,
};

static const struct of_device_id rwdt_ids[] = {
	{ .compatible = "renesas,rcar-gen3-wdt", .data = &rwdt_data_rcar_gen3, },
	{ .compatible = "renesas,r7s72100-wdt", .data = &rwdt_data_r7s72100, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rwdt_ids);

static int rwdt_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	const struct rwdt_data *data;
	struct rwdt_priv *priv;
	struct resource *res;
	unsigned long rate;
	unsigned int clks_per_sec;
	int ret, i;

	id = of_match_node(rwdt_ids, pdev->dev.of_node);
	data = id->data;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	rate = clk_get_rate(priv->clk);
	if (!rate)
		return -ENOENT;

	for (i = data->numdivs - 1; i >= 0; i--) {
		clks_per_sec = DIV_ROUND_UP(rate, data->clk_divs[i]);
		if (clks_per_sec) {
			priv->clks_per_sec = clks_per_sec;
			priv->cks = i;
			break;
		}
	}

	if (!clks_per_sec) {
		dev_err(&pdev->dev, "Can't find suitable clock divider\n");
		return -ERANGE;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	priv->wdev.info = &rwdt_ident,
	priv->wdev.ops = &rwdt_ops,
	priv->wdev.parent = &pdev->dev;
	priv->wdev.min_timeout = 1;
	//priv->wdev.max_timeout = data->countermax / clks_per_sec;
	priv->wdev.max_timeout = 65535 / clks_per_sec;
	priv->wdev.timeout = min(priv->wdev.max_timeout, RWDT_DEFAULT_TIMEOUT);
	priv->data = data;

	platform_set_drvdata(pdev, priv);
	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_set_nowayout(&priv->wdev, nowayout);

	/* This overrides the default timeout only if DT configuration was found */
	ret = watchdog_init_timeout(&priv->wdev, 0, &pdev->dev);
	if (ret)
		dev_warn(&pdev->dev, "Specified timeout value invalid, using default\n");

	ret = watchdog_register_device(&priv->wdev);
	if (ret < 0) {
		pm_runtime_put(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	if(data->rstebit >= 0){
		watchdog_set_restart_priority(&priv->wdev, 128);
	}

	return 0;
}

static int rwdt_remove(struct platform_device *pdev)
{
	struct rwdt_priv *priv = platform_get_drvdata(pdev);

	watchdog_unregister_device(&priv->wdev);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rwdt_driver = {
	.driver = {
		.name = "renesas_wdt",
		.of_match_table = rwdt_ids,
	},
	.probe = rwdt_probe,
	.remove = rwdt_remove,
};
module_platform_driver(rwdt_driver);

MODULE_DESCRIPTION("Renesas WDT Watchdog Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
