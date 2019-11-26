#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#define DRIVER_NAME "msc313-miu"

/* 
 * MSC313 MIU (memory interface unit?) - multiport ddr controller
 * 
 * The product brief for the msc313e that is available
 * doesn't detail any of the registers for this but it
 * seems to match the MIU in another MStar chip called
 * the MSB2521 that does have a leaked datasheet available.
 * That said I can't be 100% sure that all the bits in the
 * registers match what is actually in the msc313 so I'll
 * document anything that matches and not just paste the
 * whole lot here. TL;DR; there be gaps.
 * 
 * 0x1f202000?
 * In the MSB2521 datasheet this is called MIU_ATOP, miu analog?
 * 
 * 0x004 -  
 *    15 - 8    |    7 - 0
 * dqs waveform | clock waveform
 * 0xaa - x8?   | 0xaa - x8?
 * 0xcc - x4?   | 0xcc - x4?
 * 
 * 0x1f202400
 * In the MSB2521 datasheet this is called MIU_DIG, miu digital?
 * 0x000 - config0
 *         13         |      5           |
 * enter self refresh | auto refresh off |
 * 
 * 0x004 - config1
 *   15   |   14   | 13    |   12   |   7 - 6  | 5 - 4   |   3 - 2     |   1 - 0
 * cko_en | adr_en | dq_en | cke_en | columns  | banks   | bus width   | dram type
 *        |        |       |        | 0x0 - 8  | 0x0 - 2 | 0x0 - 16bit | 0x0 - SDR
 *                                  | 0x1 - 9  | 0x1 - 4 | 0x1 - 32bit | 0x1 - DDR
 *                                  | 0x2 - 10 | 0x2 - 8 | 0x2 - 64bit | 0x2 - DDR2
 *        		            |          |         |             | 0x3 - DDR3
 * 
 * The vendor suspend code writes 0xFFFF to all of these
 * but the first where it writes 0xFFFE instead
 * Presumably this is to stop requests happening while it
 * is putting the memory into low power mode.
 * 
 * 0x08c - group 0 request mask
 * 0x0cc - group 1 request mask
 * 0x10c - group 2 request mask
 * 0x14c - group 3 request mask
 * 
 * 0x1f202200
 * This isn't in the MSB2521 datasheet but it looks like a bunch
 * more group registers. 
 * 
 * The vendor pm code writes 0xFFFF these before messing with
 * the DDR right after the 4 group registers above. Hence my guess
 * is that these are a bunch of strapped on groups.
 *  
 * 0x00c - group 4 request mask?
 * 0x04c - group 5 request mask?
 */

#define REG_CONFIG1		0x4
#define REG_CONFIG1_TYPE	(BIT(1) | BIT(0))
#define REG_CONFIG1_TYPE_SDR	0
#define REG_CONFIG1_TYPE_DDR	BIT(0)
#define REG_CONFIG1_TYPE_DDR2	BIT(1)
#define REG_CONFIG1_TYPE_DDR3	(BIT(1) | BIT(0))
#define REG_CONFIG1_BUSWIDTH	(BIT(3) | BIT(2))
#define REG_CONFIG1_BANKS	(BIT(5) | BIT(4))
#define REG_CONFIG1_BANKS_SHIFT	4
#define REG_CONFIG1_COLS	(BIT(7) | BIT(6))
#define REG_CONFIG1_COLS_SHIFT	6

struct msc313_miu {
	struct device *dev;
	struct regmap *analog;
	struct regmap *digital;
	struct clk *ddrclk;
	struct clk *miuclk;
	struct regulator *ddrreg;
};

static const struct of_device_id msc313_miu_dt_ids[] = {
	{ .compatible = "mstar,msc313-miu" },
	{},
};
MODULE_DEVICE_TABLE(of, msc313_miu_dt_ids);

static const struct regmap_config msc313_miu_analog_regmap_config = {
		.name = "msc313-miu-analog",
		.reg_bits = 16,
		.val_bits = 16,
		.reg_stride = 4,
};

static const struct regmap_config msc313_miu_digital_regmap_config = {
		.name = "msc313-miu-digital",
		.reg_bits = 16,
		.val_bits = 16,
		.reg_stride = 4,
};

static int msc313_miu_probe(struct platform_device *pdev)
{
	struct msc313_miu *miu;
	struct resource *mem;
	__iomem void *base;
	unsigned int config1;
	const char *types[] = {"SDR", "DDR", "DDR2", "DDR3"};
	int banks, cols;
	
	miu = devm_kzalloc(&pdev->dev, sizeof(*miu), GFP_KERNEL);
	if (!miu)
		return -ENOMEM;
	
	miu->dev = &pdev->dev;
	
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);
	
	miu->analog = devm_regmap_init_mmio(&pdev->dev, base,
				&msc313_miu_analog_regmap_config);
	if(IS_ERR(miu->analog)){
		return PTR_ERR(miu->analog);
	}
	
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);
		
	miu->digital = devm_regmap_init_mmio(&pdev->dev, base,
			&msc313_miu_digital_regmap_config);
	if(IS_ERR(miu->digital)){
		return PTR_ERR(miu->digital);
	}
	
	miu->miuclk = devm_clk_get(&pdev->dev, "miu");
	if (IS_ERR(miu->miuclk)) {
		return PTR_ERR(miu->miuclk);
	}
	
	miu->ddrclk = devm_clk_get(&pdev->dev, "ddr");
	if (IS_ERR(miu->ddrclk)) {
			return PTR_ERR(miu->ddrclk);
	}
	
	miu->ddrreg = devm_regulator_get_optional(&pdev->dev, "ddr");
	if (IS_ERR(miu->ddrreg)){
		return PTR_ERR(miu->ddrreg);
	}

	clk_prepare_enable(miu->miuclk);
	clk_prepare_enable(miu->ddrclk);
	
	regmap_read(miu->digital, REG_CONFIG1, &config1);

	banks = 2  << ((config1 & REG_CONFIG1_BANKS) >> REG_CONFIG1_BANKS_SHIFT);
	cols = 8 + ((config1 & REG_CONFIG1_COLS) >> REG_CONFIG1_COLS_SHIFT);
	dev_info(&pdev->dev, "Memory type is %s, %d banks and %d columns", types[config1 & REG_CONFIG1_TYPE],
			banks, cols);

	return 0;
}

static int msc313_miu_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313_miu_driver = {
	.probe = msc313_miu_probe,
	.remove = msc313_miu_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = msc313_miu_dt_ids,
	},
};

module_platform_driver(msc313_miu_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mstar MSC313 MIU driver");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
