// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of_address.h>
#include <linux/module.h>

/*
 * 0x0  - ??
 * write 0x00c0 - enable
 * write 0x01b2 - disable
 *
 * 0x1c - ??
 *         1         |        0
 * set when disabled | set when enabled
 */

#define REG_MAGIC	0x0
#define REG_ENABLED	0x1c

struct msc313_upll {
	void __iomem *base;
	struct clk_hw clk_hw;
	u32 rate;
};

#define to_upll(_hw) container_of(_hw, struct msc313_upll, clk_hw)

static const struct of_device_id msc313_upll_of_match[] = {
	{
		.compatible = "mstar,msc313-upll",
	},
	{}
};
MODULE_DEVICE_TABLE(of, msc313_upll_of_match);

static int msc313_upll_is_enabled(struct clk_hw *hw){
	struct msc313_upll *upll = to_upll(hw);
	return ioread16(upll->base + REG_ENABLED) & BIT(0);
}

static unsigned long msc313_upll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate){
	struct msc313_upll *upll = to_upll(hw);
	return 0;
}

static const struct clk_ops msc313_upll_ops = {
		.is_enabled = msc313_upll_is_enabled,
		.recalc_rate = msc313_upll_recalc_rate,
};

static int msc313_upll_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct msc313_upll* upll;
	struct clk_init_data *clk_init;
	struct clk* clk;
	struct resource *mem;
	const char *parents[16];
	int numparents;
	u16 regval;

	if (!pdev->dev.of_node)
		return -ENODEV;

	id = of_match_node(msc313_upll_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	upll = devm_kzalloc(&pdev->dev, sizeof(*upll), GFP_KERNEL);
	if(!upll)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	upll->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(upll->base))
		return PTR_ERR(upll->base);

	iowrite16(0x00c0, upll->base + REG_MAGIC);
	iowrite8(0x01, upll->base + REG_ENABLED);

	numparents = of_clk_parent_fill(pdev->dev.of_node, parents, 16);
	if(numparents <= 0)
	{
		dev_info(&pdev->dev, "need some parents");
		return -EINVAL;
	}

	clk_init = devm_kzalloc(&pdev->dev, sizeof(*clk_init), GFP_KERNEL);
	if(!clk_init)
		return -ENOMEM;

	upll->clk_hw.init = clk_init;
	clk_init->name = pdev->dev.of_node->name;
	clk_init->ops = &msc313_upll_ops;
	clk_init->num_parents = numparents;
	clk_init->parent_names = parents;

	clk = clk_register(&pdev->dev, &upll->clk_hw);
	if(IS_ERR(clk)){
		printk("failed to register clk");
		return -ENOMEM;
	}

	return of_clk_add_provider(pdev->dev.of_node, of_clk_src_simple_get, clk);
}

static int msc313_upll_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313_upll_driver = {
	.driver = {
		.name = "msc313-upll",
		.of_match_table = msc313_upll_of_match,
	},
	.probe = msc313_upll_probe,
	.remove = msc313_upll_remove,
};
module_platform_driver(msc313_upll_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313 upll clock driver");
