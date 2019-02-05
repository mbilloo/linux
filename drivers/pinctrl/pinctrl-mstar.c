// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "core.h"
#include "devicetree.h"
#include "pinconf.h"
#include "pinmux.h"

#define DRIVER_NAME			"pinctrl-mstar"

/*
 * There seems to be one main pinmux block in the chip. It looks like it controls
 * the pins on a group basis and not selecting a function per pin.
 *
 * The SAR controls it's own pinmuxing.
 *
 * 0xc - UART
 *  9 8  | 7 6 |  5 4  | 3 2 |  1 0
 * UART1 |  0  | UART0 |  0  | FUART
 * 							 | 0x0 - disabled?
 * 							 | 0x1 - fuart
 * 							 | 0x2 - ??
 * 							 | 0x3 - ??
 *
 * 0x18 - SR
 * 5 4 | 3 | 2 1 0
 *  ?  | 0 |  SR
 *
 * 0x1c - PWM
 *  14   |  12   |  10   |   8   |  6   |  4   | 3 2  | 1 0
 * PWM7? | PWM6? | PWM5? | PWM4? | PWM3 | PWM2 | PWM1 | PWM0
 *                                                    | 0x0 - disabled ?
 *                                                    | 0x1 - ??
 *                                                    | 0x2 - ??
 *                                                    | 0x3 - fuart_rx
 * 0x24 - I2C
 * 5 4  | 3 2 | 1 0
 * I2C1 |  0  | I2C0
 *
 * 0x30 - SPI
 * 6 | 5 4  | 3 2 | 1 0
 * ? | SPI1 |  ?  | SPI0
 *                | 0x0 - disabled ?
 *                | 0x1 - ??
 *                | 0x2 - ??
 *                | 0x3 - fuart
 *
 * 0x3c - JTAG, ETHERNET
 * 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 |    2     | 1 0
 * ?  | 0 | ? | 0 | ? | 0 | ? | 0 | ETHERNET | JTAG
 *                                           | 0x0 - disabled ?
 *                                           | 0x1 - fuart
 *                                           | 0x2 - ??
 *                                           | 0x3 - ??
 *
 * - Toggling bits doesn't stop ethernet working :/
 *
 */

struct mstar_pinctrl {
	struct device *dev;
	struct pinctrl_desc desc;
	struct pinctrl_dev *pctl;
	void __iomem *mux;
	struct regmap* regmap;
};

struct msc313e_pinctrl_function {
	u8 reg;
	u16 mask;
	u16 value;
};

#define PIN_SAR_GPIO3	9
#define PIN_SAR_GPIO2	10
#define PIN_SAR_GPIO1	11
#define PIN_SAR_GPIO0	12
#define PIN_PM_SD_SDZ	15
#define PIN_PM_IRIN		16
#define PIN_PM_UART_RX	18
#define PIN_PM_UART_TX	19
#define PIN_PM_GPIO4	21
#define PIN_PM_SPI_CZ	22
#define PIN_PM_SPI_DI	23
#define PIN_PM_SPI_WPZ	24
#define PIN_PM_SPI_DO	25
#define PIN_PM_SPI_CK	26
#define PIN_ETH_RN		31
#define PIN_ETH_RP		32
#define PIN_ETH_TN		33
#define PIN_ETH_TP		34
#define PIN_FUART_RX	36
#define PIN_FUART_TX	37
#define PIN_FUART_CTS	38
#define PIN_FUART_RTS	39
#define PIN_I2C1_SCL	41
#define PIN_I2C1_SDA	42
#define PIN_SR_IO2		44
#define PIN_SR_IO3		45
#define PIN_SR_IO4		46
#define PIN_SR_IO5		47
#define PIN_SR_IO6		48
#define PIN_SR_IO7		49
#define PIN_SR_IO8		50
#define PIN_SR_IO9		51
#define PIN_SR_IO10		52
#define PIN_SR_IO11		53
#define PIN_SR_IO12		54
#define PIN_SR_IO13		55
#define PIN_SR_IO14		56
#define PIN_SR_IO15		57
#define PIN_SR_IO16		58
#define PIN_SR_IO17		59


static const struct pinctrl_pin_desc msc313e_pins[] = {
	PINCTRL_PIN(PIN_SAR_GPIO3,	"sar_gpio3"),
	PINCTRL_PIN(PIN_SAR_GPIO2,	"sar_gpio2"),
	PINCTRL_PIN(PIN_SAR_GPIO1,	"sar_gpio1"),
	PINCTRL_PIN(PIN_SAR_GPIO0,	"sar_gpio0"),
	PINCTRL_PIN(PIN_PM_SD_SDZ,	"pm_sd_sdz"),
	PINCTRL_PIN(PIN_PM_IRIN,	"pm_irin"),
	PINCTRL_PIN(PIN_PM_UART_RX,	"pm_uart_rx"),
	PINCTRL_PIN(PIN_PM_UART_TX,	"pm_uart_tx"),
	PINCTRL_PIN(PIN_PM_GPIO4,	"pm_gpio4"),
	PINCTRL_PIN(PIN_PM_SPI_CZ,	"pm_spi_cz"),
	PINCTRL_PIN(PIN_PM_SPI_DI,	"pm_spi_di"),
	PINCTRL_PIN(PIN_PM_SPI_WPZ,	"pm_spi_wpz"),
	PINCTRL_PIN(PIN_PM_SPI_DO,	"pm_spi_do"),
	PINCTRL_PIN(PIN_PM_SPI_CK,	"pm_spi_ck"),
	PINCTRL_PIN(PIN_ETH_RN,		"eth_rn"),
	PINCTRL_PIN(PIN_ETH_RP,		"eth_rp"),
	PINCTRL_PIN(PIN_ETH_TN,		"eth_tn"),
	PINCTRL_PIN(PIN_ETH_TP,		"eth_tp"),
	PINCTRL_PIN(PIN_FUART_RX,	"fuart_rx"),
	PINCTRL_PIN(PIN_FUART_TX,	"fuart_tx"),
	PINCTRL_PIN(PIN_FUART_CTS,	"fuart_cts"),
	PINCTRL_PIN(PIN_FUART_RTS,	"fuart_rts"),
	PINCTRL_PIN(PIN_I2C1_SCL,	"i2c1_scl"),
	PINCTRL_PIN(PIN_I2C1_SDA,	"i2c1_sda"),
	PINCTRL_PIN(PIN_SR_IO2,		"sr_io2"),
	PINCTRL_PIN(PIN_SR_IO3,		"sr_io3"),
	PINCTRL_PIN(PIN_SR_IO4,		"sr_io4"),
	PINCTRL_PIN(PIN_SR_IO5,		"sr_io5"),
	PINCTRL_PIN(PIN_SR_IO6,		"sr_io6"),
	PINCTRL_PIN(PIN_SR_IO7,		"sr_io7"),
	PINCTRL_PIN(PIN_SR_IO8,		"sr_io8"),
	PINCTRL_PIN(PIN_SR_IO9,		"sr_io9"),
	PINCTRL_PIN(PIN_SR_IO10,	"sr_io10"),
	PINCTRL_PIN(PIN_SR_IO11,	"sr_io11"),
	PINCTRL_PIN(PIN_SR_IO12,	"sr_io12"),
	PINCTRL_PIN(PIN_SR_IO13,	"sr_io13"),
	PINCTRL_PIN(PIN_SR_IO14,	"sr_io14"),
	PINCTRL_PIN(PIN_SR_IO15,	"sr_io15"),
	PINCTRL_PIN(PIN_SR_IO16,	"sr_io16"),
	PINCTRL_PIN(PIN_SR_IO17,	"sr_io17"),
};

static int pm_uart_pins[] = { PIN_PM_UART_RX, PIN_PM_UART_TX };
static int eth_pins[] = { PIN_ETH_RN, PIN_ETH_RP, PIN_ETH_TN, PIN_ETH_TP };
static int fuart_pins[] = { PIN_FUART_RX, PIN_FUART_TX,PIN_FUART_CTS, PIN_FUART_RTS };
static int pwm0_pins[] = { PIN_FUART_RX };
static int pwm1_pins[] = { PIN_FUART_TX };

static struct msc313e_pinctrl_function pwm0_function = {
	.reg = 0x1c,
	.mask = BIT(1) | BIT(0),
	.value = BIT(1) | BIT(0),
};

static struct msc313e_pinctrl_function pwm1_function = {
	.reg = 0x1c,
	.mask = BIT(3) | BIT(2),
	.value = BIT(3) | BIT(2),
};

static struct msc313e_pinctrl_function eth_function = {
	.reg = 0x3c,
	.mask = BIT(2),
	.value = BIT(2),
};

static int mstar_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np,
								map, num_maps,
								PIN_MAP_TYPE_INVALID);
}

static void mstar_dt_free_map(struct pinctrl_dev *pctldev,
			     struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops mstar_pinctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= mstar_dt_node_to_map,
	.dt_free_map		= mstar_dt_free_map,
};

static int mstar_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			   unsigned int group)
{
	struct mstar_pinctrl *pinctrl = pctldev->driver_data;
	struct group_desc *groupdesc = pinctrl_generic_get_group(pctldev, group);
	struct msc313e_pinctrl_function *function;
	int ret = 0;

	function = groupdesc->data;

	if(function != NULL){
		printk("updating mux reg %x\n", (unsigned) function->reg);
		ret = regmap_update_bits(pinctrl->regmap, function->reg,
				function->mask, function->value);
		if(ret)
			printk("failed to update register\n");
	}
	else {
		printk("missing function data\n");
	}

	return ret;
}

static const struct pinmux_ops mstar_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= mstar_set_mux,
	.strict			= true,
};

static int mstar_pinctrl_parse_groups(struct mstar_pinctrl *pinctrl){
	int ret;
	ret = pinctrl_generic_add_group(pinctrl->pctl, "pm_uart",
				pm_uart_pins, ARRAY_SIZE(pm_uart_pins), NULL);
	ret = pinctrl_generic_add_group(pinctrl->pctl, "eth",
			eth_pins, ARRAY_SIZE(eth_pins), &eth_function);
	ret = pinctrl_generic_add_group(pinctrl->pctl, "fuart",
			fuart_pins, ARRAY_SIZE(fuart_pins), NULL);
	ret = pinctrl_generic_add_group(pinctrl->pctl, "pwm0",
			pwm1_pins, ARRAY_SIZE(pwm0_pins), &pwm0_function);
	ret = pinctrl_generic_add_group(pinctrl->pctl, "pwm1",
			pwm1_pins, ARRAY_SIZE(pwm1_pins), &pwm1_function);
	return ret;
}

static int mstar_pinctrl_parse_functions(struct mstar_pinctrl *pinctrl){
	struct device_node* root = pinctrl->dev->of_node;
	struct device_node* funcnode = NULL;
	const char *funcname;
	const char **groups;
	int nfuncs = of_get_child_count(root);
	int i,j, ret, numpins, numgroups;

	for(i = 0; i < nfuncs; i++){
		funcnode = of_get_next_child(root, funcnode);
		of_property_read_string(funcnode, "function", &funcname);

		dev_info(pinctrl->dev, "parsing function %s", funcname);

		numpins = of_property_count_strings(funcnode, "pins");
		numgroups = of_property_count_strings(funcnode, "groups");
		if(numgroups <= 0){
			dev_err(pinctrl->dev, "need some groups");
			goto out;
		}

		groups = devm_kzalloc(pinctrl->dev, sizeof(char*) * numgroups, GFP_KERNEL);
		for(j = 0; j < numgroups; j++){
			ret = of_property_read_string_index(funcnode, "groups", j, &groups[j]);
			if(ret){
				dev_err(pinctrl->dev, "couldn't get group name");
				goto out;
			}
		}

		ret = pinmux_generic_add_function(pinctrl->pctl,funcname,
											groups,numgroups,NULL);
		if(ret < 0){
			dev_err(pinctrl->dev, "failed to add function: %d", ret);
			goto out;
		}
	}
	out:
	return ret;
}

static const struct regmap_config msc313e_pinctrl_regmap_config = {
		.name = "msc313e-pinctrl",
		.reg_bits = 16,
		.val_bits = 16,
		.reg_stride = 4,
};

static int mstar_pinctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct mstar_pinctrl *pinctrl;
	struct resource *res;

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pinctrl);

	pinctrl->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pinctrl->mux = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->mux))
		return PTR_ERR(pinctrl->mux);

	pinctrl->regmap = devm_regmap_init_mmio(pinctrl->dev, pinctrl->mux,
			&msc313e_pinctrl_regmap_config);
	if(IS_ERR(pinctrl->regmap)){
		dev_err(pinctrl->dev, "failed to register regmap");
		return PTR_ERR(pinctrl->regmap);
	}

	pinctrl->desc.name = DRIVER_NAME;
	pinctrl->desc.pctlops = &mstar_pinctrl_ops;
	pinctrl->desc.pmxops = &mstar_pinmux_ops;
	pinctrl->desc.owner = THIS_MODULE;
	pinctrl->desc.pins = msc313e_pins;
	pinctrl->desc.npins = ARRAY_SIZE(msc313e_pins);

	ret = devm_pinctrl_register_and_init(pinctrl->dev, &pinctrl->desc,
					     pinctrl, &pinctrl->pctl);

	if (ret) {
		dev_err(pinctrl->dev, "failed to register pinctrl\n");
		return ret;
	}

	ret = mstar_pinctrl_parse_functions(pinctrl);
	ret = mstar_pinctrl_parse_groups(pinctrl);

	ret = pinctrl_enable(pinctrl->pctl);
	if (ret)
		dev_err(pinctrl->dev, "failed to enable pinctrl\n");

	return 0;
}

static const struct of_device_id mstar_pinctrl_of_match[] = {
	{
		.compatible	= "mstar,msc313e-pinctrl",
	},
	{ }
};

static struct platform_driver mstar_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mstar_pinctrl_of_match,
	},
	.probe = mstar_pinctrl_probe,
};

static int __init mstar_pinctrl_init(void)
{
	return platform_driver_register(&mstar_pinctrl_driver);
}
core_initcall(mstar_pinctrl_init);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("Pin controller driver for MStar MSC313E SoCs");
MODULE_LICENSE("GPL v2");
