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
 * The clkgen block controls a bunch of clock gates and muxes
 * Each register contains gates, muxes and some sort of anti-glitch
 * control.
 *
 * This driver controls the gates and muxes packed into a single register.
 */

struct msc313e_clkgen_mux {
	void __iomem *base;
	struct clk_hw clk_hw;
	u8 shift;
};

#define to_clkgen_mux(_hw) container_of(_hw, struct msc313e_clkgen_mux, clk_hw)

static const struct of_device_id msc313e_clkgen_mux_of_match[] = {
	{
		.compatible = "mstar,msc313e-clkgen-mux",
	},
	{}
};
MODULE_DEVICE_TABLE(of, msc313e_clkgen_mux_of_match);

static int msc313e_clkgen_mux_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct msc313e_clkgen_mux* clkgen_mux;
	struct resource *base;
	struct clk_gate *gate;
	struct clk_mux *mux;
	struct clk* clk;
	int numparents, numshifts, nummuxes, muxindex;
	struct clk_onecell_data *clk_data;
	const char *name;
	const char *parents[32];
	const char **muxparents;
	u32 gateshift, muxshift, muxwidth, muxclockoffset, muxnumclocks, outputflags;

	if (!pdev->dev.of_node)
		return -ENODEV;

	id = of_match_node(msc313e_clkgen_mux_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	numparents = of_clk_parent_fill(pdev->dev.of_node, parents, ARRAY_SIZE(parents));

	if(numparents <= 0)
	{
		dev_info(&pdev->dev, "need some parents");
		return -EINVAL;
	}

	base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	nummuxes = of_property_count_strings(pdev->dev.of_node,
			"clock-output-names");
	if (!nummuxes) {
		dev_info(&pdev->dev, "output names need to be specified");
		return -ENODEV;
	}

	numshifts = of_property_count_u32_elems(pdev->dev.of_node, "shifts");
	if(!numshifts){
		dev_info(&pdev->dev, "shifts need to be specified");
		return -ENODEV;
	}

	if(numshifts != nummuxes){
		dev_info(&pdev->dev, "number of shifts must match the number of outputs");
		return -EINVAL;
	}

	clk_data = devm_kzalloc(&pdev->dev, sizeof(struct clk_onecell_data),
			GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->clk_num = nummuxes;
	clk_data->clks = devm_kcalloc(&pdev->dev, nummuxes, sizeof(struct clk *),
			GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	for (muxindex = 0; muxindex < nummuxes; muxindex++) {
		clkgen_mux = devm_kzalloc(&pdev->dev, sizeof(*clkgen_mux), GFP_KERNEL);
		if (!clkgen_mux)
			return -ENOMEM;

		clkgen_mux->base = base;

		of_property_read_string_index(pdev->dev.of_node, "clock-output-names",
				muxindex, &name);

		gate = devm_kzalloc(&pdev->dev, sizeof(*gate), GFP_KERNEL);
		if(!gate)
			return -ENOMEM;

		gate->reg = base;
		of_property_read_u32_index(pdev->dev.of_node, "shifts", muxindex, &gateshift);
		gate->bit_idx = gateshift;
		gate->flags = CLK_GATE_SET_TO_DISABLE;

		if(of_property_read_u32_index(pdev->dev.of_node, "mux-shifts", muxindex, &muxshift) == 0 &&
				of_property_read_u32_index(pdev->dev.of_node, "mux-widths", muxindex, &muxwidth) == 0){
			mux = devm_kzalloc(&pdev->dev, sizeof(*mux), GFP_KERNEL);
			if(!mux)
				return -ENOMEM;
			mux->reg = base;
			mux->shift = muxshift;
			mux->mask = ~((~0 >> muxwidth) << muxwidth);
			mux->flags = CLK_MUX_ROUND_CLOSEST;
		}
		else {
			dev_info(&pdev->dev, "mux-shifts or mux-widths missing\n");
			mux = NULL;
		}

		if(of_property_read_u32_index(pdev->dev.of_node, "mux-ranges", muxindex * 2, &muxclockoffset) == 0 &&
						of_property_read_u32_index(pdev->dev.of_node, "mux-ranges", (muxindex * 2) + 1, &muxnumclocks) == 0){
			muxparents = parents + muxclockoffset;
			muxnumclocks = muxnumclocks;
			dev_dbg(&pdev->dev, "using clocks %d -> %d for mux", (int)(muxclockoffset), (int)(muxclockoffset + muxnumclocks));
		}
		else {
			muxparents = parents;
			muxnumclocks = numparents;
			dev_dbg(&pdev->dev, "clock range not specified, mux will use all clocks");
		}

		outputflags = 0;
		if(of_property_read_u32_index(pdev->dev.of_node, "output-flags", muxindex, &outputflags) == 0){
			dev_dbg(&pdev->dev, "applying flags %x to output %d", outputflags, muxindex);
		}

		clk = clk_register_composite(&pdev->dev, name,
				muxparents, muxnumclocks,
				mux != NULL ? &mux->hw : NULL, mux != NULL ? &clk_mux_ops : NULL,
				NULL, NULL,
				&gate->hw, &clk_gate_ops,
				outputflags);
		if (IS_ERR(clk)) {
			return PTR_ERR(clk);
		}

		clk_data->clks[muxindex] = clk;
	}

	return of_clk_add_provider(pdev->dev.of_node, of_clk_src_onecell_get, clk_data);
}

static int msc313e_clkgen_mux_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_clkgen_mux_driver = {
	.driver = {
		.name = "msc313e-clkgen-mux",
		.of_match_table = msc313e_clkgen_mux_of_match,
	},
	.probe = msc313e_clkgen_mux_probe,
	.remove = msc313e_clkgen_mux_remove,
};
module_platform_driver(msc313e_clkgen_mux_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313e clkgen mux driver");
