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
 * CPU clock seems to come from a PLL that has registers at 0x1f206500
 * 0x40 -- LPF low. Seems to store one half of the clock transition
 * 0x44 /
 * 0x48 -- LPF high. Seems to store one half of the clock transition
 * 0x4c /
 * 0x50 -- code says "toggle lpf enable"
 * 0x54 -- mu?
 * 0x5c -- lpf_update_count?
 * 0x60 -- code says "switch to LPF". Clock source config? Register bank?
 * 0x64 -- code says "from low to high" which seems to mean transition from LPF low to LPF high.
 * 0x74 -- Seems to be the PLL lock status bit
 * 0x80 -- Seems to be the current frequency
 * 0x84 /
 */

struct msc313e_cpuclk {
	void __iomem *base;
	struct clk_hw clk_hw;
	u32 rate;
};

#define to_cpuclk(_hw) container_of(_hw, struct msc313e_cpuclk, clk_hw)

struct freqregisters {
	u32 frequency;
	u16 bottom, top;
};

#define REG_LPF_LOW_BOTTOM	   0x40
#define REG_LPF_LOW_TOP		   0x44
#define REG_LPF_HIGH_BOTTOM	   0x48
#define REG_LPF_HIGH_TOP	   0x4c
#define REG_LPF_TOGGLE		   0x50
#define REG_LPF_MYSTERYTWO	   0x54
#define REG_LPF_UPDATE_COUNT   0x5c
#define REG_LPF_MYSTERYONE	   0x60
#define REG_LPF_TRANSITIONCTRL 0x64
#define REG_LPF_LOCK		   0x74

static void msc313_cpuclk_setfreq(struct msc313e_cpuclk *cpuclk, const struct freqregisters *freqreg) {
	printk("changing cpu clock frequency\n");
	iowrite16(freqreg->bottom, cpuclk->base + REG_LPF_HIGH_BOTTOM);
	iowrite16(freqreg->top, cpuclk->base + REG_LPF_HIGH_TOP);
	iowrite16(0x1, cpuclk->base + REG_LPF_MYSTERYONE);
	iowrite16(0x6, cpuclk->base + REG_LPF_MYSTERYTWO);
	iowrite16(0x8, cpuclk->base + REG_LPF_UPDATE_COUNT);
	iowrite16(BIT(12), cpuclk->base + REG_LPF_TRANSITIONCTRL);

	iowrite16(0, cpuclk->base + REG_LPF_TOGGLE);
	iowrite16(1, cpuclk->base + REG_LPF_TOGGLE);

	while(!(ioread16(cpuclk->base + REG_LPF_LOCK)));

	iowrite16(0, cpuclk->base + REG_LPF_TOGGLE);

	iowrite16(freqreg->bottom, cpuclk->base + REG_LPF_LOW_BOTTOM);
	iowrite16(freqreg->top, cpuclk->base + REG_LPF_LOW_TOP);

	cpuclk->rate = freqreg->frequency;
}

static const struct of_device_id msc313e_cpuclk_of_match[] = {
	{
		.compatible = "mstar,msc313e-cpuclk",
	},
	{}
};
MODULE_DEVICE_TABLE(of, msc313e_cpuclk_of_match);

static unsigned long msc313e_cpuclk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate){
	struct msc313e_cpuclk *cpuclk = to_cpuclk(hw);
	return cpuclk->rate;
}

static const struct clk_ops msc313e_cpuclk_ops = {
		.recalc_rate = msc313e_cpuclk_recalc_rate,
};

static const struct freqregisters fourhundredmhz = {
		.frequency = 400000000,
		.bottom =  0xAE14,
		.top = 0x0067
};

static const struct freqregisters sixhundredmhz = {
		.frequency = 600000000,
		.bottom =  0x1EB8,
		.top = 0x0045
};

static const struct freqregisters eighthundredmhz = {
		.frequency = 800000000,
		.bottom =  0xD70A,
		.top = 0x0033
};

static const struct freqregisters oneghz = {
		.frequency = 1000000000,
		.bottom =  0x78d4,
		.top = 0x0029
};

static int msc313e_cpuclk_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct msc313e_cpuclk* cpuclk;
	struct clk_init_data *clk_init;
	struct clk* clk;
	struct resource *mem;

	if (!pdev->dev.of_node)
		return -ENODEV;

	id = of_match_node(msc313e_cpuclk_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	cpuclk = devm_kzalloc(&pdev->dev, sizeof(*cpuclk), GFP_KERNEL);
	if(!cpuclk)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cpuclk->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(cpuclk->base))
		return PTR_ERR(cpuclk->base);

	clk_init = devm_kzalloc(&pdev->dev, sizeof(*clk_init), GFP_KERNEL);
	if(!clk_init)
		return -ENOMEM;

	cpuclk->clk_hw.init = clk_init;
	clk_init->name = pdev->dev.of_node->name;
	clk_init->ops = &msc313e_cpuclk_ops;

	clk = clk_register(&pdev->dev, &cpuclk->clk_hw);
	if(IS_ERR(clk)){
		printk("failed to register clk");
		return -ENOMEM;
	}

	msc313_cpuclk_setfreq(cpuclk, &oneghz);

	return of_clk_add_provider(pdev->dev.of_node, of_clk_src_simple_get, clk);
}

static int msc313e_cpuclk_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_cpuclk_driver = {
	.driver = {
		.name = "msc313e-cpuclk",
		.of_match_table = msc313e_cpuclk_of_match,
	},
	.probe = msc313e_cpuclk_probe,
	.remove = msc313e_cpuclk_remove,
};
module_platform_driver(msc313e_cpuclk_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313e cpu clock driver");
