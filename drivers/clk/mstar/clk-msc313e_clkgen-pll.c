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
 - 0x1c4(0x71) - pll force on bits
 - 0x1c8(0x72) - pll force off bits
 - 0x1cc(0x73) - pll en rd bits
      15   |       14  |     13   |     12   |     11   |     10   |     9    |     8
  pll rv1  |  mpll 86  | mpll 124 | mpll 123 | mpll 144 | mpll 172 | mpll 216 | mpll 288
      7    |     6    |     5    |     4    |     3    |     2    |     1    |     0
  mpll 345 | mpll 432 | utmi 480 | utmi 240 | utmi 192 | utmi 160 | upll 320 | upll 384
*/

struct msc313e_clkgen_pll {
	void __iomem *base;
	u8 index;
	u32 rate;
	struct clk_hw clk_hw;
};

#define to_clkgen_pll(_hw) container_of(_hw, struct msc313e_clkgen_pll, clk_hw)

static const struct of_device_id msc313e_clkgen_pll_of_match[] = {
	{
		.compatible = "mstar,msc313e-clkgen-pll",
	},
	{}
};
MODULE_DEVICE_TABLE(of, msc313e_clkgen_pll_of_match);



static	int	msc313_clkgen_pll_enable(struct clk_hw *hw)
{
	struct msc313e_clkgen_pll *clkgen_pll = to_clkgen_pll(hw);
	printk("enabling pll %s\n", hw->init->name);
	return 0;
}

static void	msc313e_clkgen_pll_disable(struct clk_hw *hw)
{
	struct msc313e_clkgen_pll *clkgen_pll = to_clkgen_pll(hw);
	printk("disabling pll %s\n", hw->init->name);
}

static int	msc313e_clkgen_pll_is_enabled(struct clk_hw *hw)
{
	struct msc313e_clkgen_pll *clkgen_pll = to_clkgen_pll(hw);
	printk("checking pll %s\n", hw->init->name);
	return 1;
}

static unsigned long msc313e_clkgen_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct msc313e_clkgen_pll *clkgen_pll = to_clkgen_pll(hw);
	return clkgen_pll->rate;
}

static const struct clk_ops msc313e_clkgen_pll_ops = {
		.enable = msc313_clkgen_pll_enable,
		.disable = msc313e_clkgen_pll_disable,
		.is_enabled = msc313e_clkgen_pll_is_enabled,
		.recalc_rate = msc313e_clkgen_pll_recalc_rate
};

static int msc313e_clkgen_pll_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct msc313e_clkgen_pll* clkgen_pll;
	struct clk_init_data *clk_init;
	struct clk* clk;
	struct resource *mem, *base;
	int numoutputs, numrates, pllindex;
	struct clk_onecell_data *clk_data;

	if (!pdev->dev.of_node)
		return -ENODEV;

	id = of_match_node(msc313e_clkgen_pll_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	numoutputs = of_property_count_strings(pdev->dev.of_node, "clock-output-names");
	if(!numoutputs){
		dev_info(&pdev->dev, "output names need to be specified");
		return -ENODEV;
	}

	if(numoutputs > 16){
		dev_info(&pdev->dev, "too many output names");
		return -EINVAL;
	}

	numrates = of_property_count_u32_elems(pdev->dev.of_node, "clock-rates");
	if(!numrates){
		dev_info(&pdev->dev, "clock rates need to be specified");
		return -ENODEV;
	}

	if(numrates != numoutputs){
		dev_info(&pdev->dev, "number of clock rates must match the number of outputs");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = devm_kzalloc(&pdev->dev, sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->clk_num = numoutputs;
	clk_data->clks = devm_kcalloc(&pdev->dev, numoutputs, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	for (pllindex = 0; pllindex < numoutputs; pllindex++) {
		clkgen_pll = devm_kzalloc(&pdev->dev, sizeof(*clkgen_pll), GFP_KERNEL);
		if (!clkgen_pll)
			return -ENOMEM;

		clkgen_pll->base = base;
		clkgen_pll->index = pllindex;
		of_property_read_u32_index(pdev->dev.of_node, "clock-rates",
				pllindex, &clkgen_pll->rate);

		clk_init = devm_kzalloc(&pdev->dev, sizeof(*clk_init), GFP_KERNEL);
		if (!clk_init)
			return -ENOMEM;

		clkgen_pll->clk_hw.init = clk_init;
		of_property_read_string_index(pdev->dev.of_node,
				"clock-output-names", pllindex, &clk_init->name);
		clk_init->ops = &msc313e_clkgen_pll_ops;

		dev_info(&pdev->dev, "registering pll %s\n", clk_init->name);

		clk = clk_register(&pdev->dev, &clkgen_pll->clk_hw);
		if (IS_ERR(clk)) {
			printk("failed to register clk");
			return -ENOMEM;
		}
		clk_data->clks[pllindex] = clk;
	}

	return of_clk_add_provider(pdev->dev.of_node, of_clk_src_onecell_get, clk_data);
}

static int msc313e_clkgen_pll_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_clkgen_pll_driver = {
	.driver = {
		.name = "msc313e-clkgen-pll",
		.of_match_table = msc313e_clkgen_pll_of_match,
	},
	.probe = msc313e_clkgen_pll_probe,
	.remove = msc313e_clkgen_pll_remove,
};
module_platform_driver(msc313e_clkgen_pll_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313e clkgen pll driver");
