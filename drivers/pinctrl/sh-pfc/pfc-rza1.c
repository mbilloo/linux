/*
 * Copyright (C) 2013-2014  Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <asm/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/rza1pfc.h>

#include "../pinctrl-utils.h"

MODULE_DESCRIPTION("RZA1 Pinctrl Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Renesas Solutions Corp.");
MODULE_ALIAS("platform:pfc-rza1");

#define GPIO_CHIP_NAME "RZA1_INTERNAL_PFC"

#define PORT_OFFSET	0x4
#define PORT(p)		(0x0000 + (p) * PORT_OFFSET)
#define PPR(p)		(0x0200 + (p) * PORT_OFFSET)
#define PM(p)		(0x0300 + (p) * PORT_OFFSET)
#define PMC(p)		(0x0400 + (p) * PORT_OFFSET)
#define PFC(p)		(0x0500 + (p) * PORT_OFFSET)
#define PFCE(p)		(0x0600 + (p) * PORT_OFFSET)
#define PFCAE(p)	(0x0a00 + (p) * PORT_OFFSET)
#define PIBC(p)		(0x4000 + (p) * PORT_OFFSET)
#define PBDC(p)		(0x4100 + (p) * PORT_OFFSET)
#define PIPC(p)		(0x4200 + (p) * PORT_OFFSET)

enum {
	REG_PFC = 0,
	REG_PFCE,
	REG_PFCAE,
	REG_NUM,
};

static bool mode_regset[][REG_NUM] = {
	/* PFC,	PFCE,	PFCAE */
	{false,	false,	false	}, /* port mode */
	{false,	false,	false	}, /* alt true */
	{true,	false,	false	}, /* alt 2 */
	{false,	true,	false	}, /* alt 3 */
	{true,	true,	false	}, /* alt 4 */
	{false,	false,	true	}, /* alt 5 */
	{true,	false,	true	}, /* alt 6 */
	{false,	true,	true	}, /* alt 7 */
	{true,	true,	true	}, /* alt 8 */
};

struct rza1pfc_function {
	const char *name;
	unsigned *pins;
	unsigned *modes;
	unsigned *dirs;
	unsigned npins;
};

struct rza1pfc {
	struct device *dev;
	void __iomem *base;
	struct mutex mutex;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_desc pctl_desc;
	struct pinctrl_gpio_range gpio_range;

	//
	struct rza1pfc_function *functions;
	int	nfunctions;
};

struct rza1pfc_gpio {
	struct rza1pfc* pfc;
	unsigned port;
	unsigned npins;
};

struct rza1pfc_config {
	unsigned pin;
	unsigned mode;
	unsigned direction;
};

static inline int _bit_modify(void __iomem *addr, int bit, bool data)
{
	__raw_writel((__raw_readl(addr) & ~(0x1 << bit)) | (data << bit), addr);
	return 0;
}

static inline int bit_modify(void __iomem *base, unsigned int addr, int bit, bool data)
{
	return _bit_modify(base + addr, bit, data);
}

static int set_direction(void __iomem *base, unsigned int port, int bit, unsigned dir)
{
	if ((port == 0) && (dir != RZA1PFC_DIR_IN))	/* p0 is input only */
		return -1;

	if (dir == RZA1PFC_DIR_IN) {
		bit_modify(base, PM(port), bit, true);
		bit_modify(base, PIBC(port), bit, true);
	} else {
		bit_modify(base, PM(port), bit, false);
		bit_modify(base, PIBC(port), bit, false);
	}

	return 0;
}

static int chip_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct rza1pfc_gpio *gpio;
	unsigned int d;

	gpio = gpiochip_get_data(chip);
	d = __raw_readl(gpio->pfc->base + PPR(gpio->port));

	return (d &= (0x1 << pin)) ? 1 : 0;
}

static void chip_gpio_set(struct gpio_chip *chip, unsigned pin, int val)
{
	struct rza1pfc_gpio *gpio;

	gpio = gpiochip_get_data(chip);

	if (gpio->port <= 0)	/* p0 is input only */
		return;

	bit_modify(gpio->pfc->base, PORT(gpio->port), pin, val);
	return;
}

static int chip_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct rza1pfc_gpio *gpio;

	gpio = gpiochip_get_data(chip);

	mutex_lock(&gpio->pfc->mutex);
	set_direction(&gpio->pfc->base, gpio->port, pin, RZA1PFC_DIR_IN);
	mutex_unlock(&gpio->pfc->mutex);

	return 0;
}

static int chip_direction_output(struct gpio_chip *chip, unsigned pin,
				int val)
{
	struct rza1pfc_gpio *gpio;

	gpio = gpiochip_get_data(chip);

	if (gpio->port == 0)	/* case : p0 is input only*/
		return -1;

	mutex_lock(&gpio->pfc->mutex);
	bit_modify(gpio->pfc->base, PORT(gpio->port), pin, val);
	set_direction(gpio->pfc->base, gpio->port, pin, RZA1PFC_DIR_OUT);
	mutex_unlock(&gpio->pfc->mutex);

	return 0;
}

static int set_mode(void __iomem *base, unsigned int port, int bit, int mode)
{
	bit_modify(base, PFC(port), bit, mode_regset[mode][0]);
	bit_modify(base, PFCE(port), bit, mode_regset[mode][1]);
	bit_modify(base, PFCAE(port), bit, mode_regset[mode][2]);
	return 0;
}


/*
 * @pinnum: a pin number.
 * @mode:   port mode or alternative N mode.
 * @dir:    Kind of I/O mode and data direction and PBDC and Output Level.
 *          PIPC enable SoC IP to control a direction.
 */

static int r7s72100_pfc_pin_assign(struct device *dev, void __iomem *base, unsigned pin, unsigned mode,
			unsigned dir)
{
	int port = (pin >> RZA1PFC_PORT_SHIFT) & 0xf, bit = pin & 0xf;

	dev_info(dev, "setting mux for %d:%d\n", port, bit);

	/* Error is less than 0 port */
	if (port < 0)
		return -1;

	/* Port 0 there is only PMC and PIBC control register */
	if (port == 0) {
		/* Port initialization */
		bit_modify(base, PIBC(port), bit, false);	/* Input buffer block */
		bit_modify(base, PMC(port), bit, false);	/* Port mode */

		/* Port Mode */
		if (mode == RZA1PFC_MODE_GPIO) {
			if (dir == RZA1PFC_DIR_IN) {
				/* PIBC Setting : Input buffer allowed */
				bit_modify(base, PIBC(port), bit, true);
			} else {
				return -1;	/* P0 Portmode is input only */
			}
		/* Alternative Mode */
		} else {
			if ((bit == 4) || (bit == 5)) {
				/* PMC Setting : Alternative mode */
				bit_modify(base, PMC(port), bit, true);
			} else {
				return -1;	/* P0 Altmode P0_4,P0_5 only */
			}
		}
		return 0;
	}

	/* Port initialization */
	bit_modify(base, PIBC(port), bit, false);	/* Inputbuffer block */
	bit_modify(base, PBDC(port), bit, false);	/* Bidirection disabled */
	bit_modify(base, PM(port), bit, true);	/* Input mode(output disabled)*/
	bit_modify(base, PMC(port), bit, false);	/* Port mode */
	bit_modify(base, PIPC(port), bit, false);	/* software I/Ocontrol. */

	/* PBDC Setting */
	if ((dir == RZA1PFC_DIIO_PBDC_EN) || (dir == RZA1PFC_SWIO_OUT_PBDCEN))
		bit_modify(base, PBDC(port), bit, true);	/* Bidirection enable */

	/* Port Mode */
	if (mode == RZA1PFC_MODE_GPIO) {
		if (dir == RZA1PFC_DIR_IN) {
			bit_modify(base, PIBC(port), bit, true); /*Inputbuffer allow*/
		} else if (dir == RZA1PFC_PORT_OUT_LOW) {
			bit_modify(base, PORT(port), bit, false); /*Output low level*/
			bit_modify(base, PM(port), bit, false);  /* Output mode */
		} else if (dir == RZA1PFC_PORT_OUT_HIGH) {
			bit_modify(base, PORT(port), bit, true); /*Output high level*/
			bit_modify(base, PM(port), bit, false);  /* Output mode */
		} else {
			return -1;
		}
	/* direct I/O control & software I/Ocontrol */
	} else {
		/* PFC,PFCE,PFCAE Setting */
		set_mode(base, port, bit, mode); /* Alternative Function Select */

		/* PIPC Setting */
		if ((dir == RZA1PFC_DIIO_PBDC_DIS) || (dir == RZA1PFC_DIIO_PBDC_EN))
			bit_modify(base, PIPC(port), bit, true); /* direct I/O cont */
		/* PMC Setting */
		bit_modify(base, PMC(port), bit, true);	/* Alternative Mode */

		/* PM Setting : Output mode (output enabled)*/
		if ((dir == RZA1PFC_SWIO_OUT_PBDCDIS) || (dir == RZA1PFC_SWIO_OUT_PBDCEN))
			bit_modify(base, PM(port), bit, false);
	}

	return 0;
}

/* Pinctrl functions */
static int rza1pfc_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
		struct rza1pfc *pinctrl = pinctrl_dev_get_drvdata(pctldev);
		struct pinctrl_map *new_map;

		new_map = devm_kzalloc(pinctrl->dev, sizeof(*new_map), GFP_KERNEL);
		if (!new_map)
			return -ENOMEM;

		*map = new_map;
		*num_maps = 1;

		new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[0].data.mux.function = np_config->name;
		new_map[0].data.mux.group = np_config->name;

		return 0;
}

static int rza1pfc_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rza1pfc *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	return pinctrl->nfunctions;
}

static const char *rza1pfc_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned group)
{
	struct rza1pfc *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	return pinctrl->functions[group].name;
}

static int rza1pfc_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned group,
				      const unsigned **pins,
				      unsigned *num_pins)
{
	struct rza1pfc *pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pinctrl->functions[group].pins;
	*num_pins = pinctrl->functions[group].npins;
	return 0;
}

static const struct pinctrl_ops rza1pfc_pctrl_ops = {
	.dt_node_to_map		= rza1pfc_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
	.get_groups_count	= rza1pfc_pctrl_get_groups_count,
	.get_group_name		= rza1pfc_pctrl_get_group_name,
	.get_group_pins		= rza1pfc_pctrl_get_group_pins,
};

/* Pinmux functions */

static int rza1pfc_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	struct rza1pfc *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->nfunctions;
}

static const char *rza1pfc_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned selector)
{
	struct rza1pfc *pctl = pinctrl_dev_get_drvdata(pctldev);
	return pctl->functions[selector].name;
}

static int rza1pfc_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned function,
				     const char * const **groups,
				     unsigned * const num_groups)
{
	struct rza1pfc *pctl = pinctrl_dev_get_drvdata(pctldev);
	*groups = &pctl->functions[function].name;
	*num_groups = 1;
	return 0;
}

static int rza1pfc_pmx_set_mux(struct pinctrl_dev *pctldev,
			    unsigned function,
			    unsigned group)
{
	struct rza1pfc *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct rza1pfc_function *func = &pctl->functions[function];
	int i;

	dev_info(pctl->dev, "rza1pfc_pmx_set_mux %u %d\n", function, group);

	mutex_lock(&pctl->mutex);
	for(i = 0; i < func->npins; i++){
		dev_info(pctl->dev, "pin %d %u %u %u\n", i, func->pins[i], func->modes[i], func->dirs[i]);
		r7s72100_pfc_pin_assign(pctl->dev, pctl->base, func->pins[i], func->modes[i], func->dirs[i]);
	}
	mutex_unlock(&pctl->mutex);

	return 0;
}

static int rza1pfc_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned gpio,
			bool input)
{
	return -EINVAL;
}

static const struct pinmux_ops rza1pfc_pmx_ops = {
	.get_functions_count	= rza1pfc_pmx_get_funcs_cnt,
	.get_function_name	= rza1pfc_pmx_get_func_name,
	.get_function_groups	= rza1pfc_pmx_get_func_groups,
	.set_mux		= rza1pfc_pmx_set_mux,
	.gpio_set_direction	= rza1pfc_pmx_gpio_set_direction,
};


/* Pinconf functions */
static int rza1pfc_pconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *config)
{
	return -EINVAL;
}

static int rza1pfc_pconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				 unsigned long *configs, unsigned num_configs)
{
	return -EINVAL;
}

static void rza1pfc_pconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s,
				 unsigned int pin)
{
}


static const struct pinconf_ops rza1pfc_pconf_ops = {
	.pin_config_group_get	= rza1pfc_pconf_group_get,
	.pin_config_group_set	= rza1pfc_pconf_group_set,
	.pin_config_dbg_show	= rza1pfc_pconf_dbg_show,
};

static int rza1pfc_gpio_registerport(struct platform_device *pdev, struct device_node *ofnode,
		struct rza1pfc *rza1pinctrl, unsigned port, unsigned pins){
	struct gpio_chip* gpiochip;
	struct rza1pfc_gpio* gpio;
	const char* *names;
	char* namestrings;
	int i, retval = 0;

	gpiochip = devm_kzalloc(&pdev->dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
		if (!gpio)
			return -ENOMEM;

	names = devm_kzalloc(&pdev->dev, sizeof(*names) * pins, GFP_KERNEL);
		if (!names)
			return -ENOMEM;

	namestrings = devm_kzalloc(&pdev->dev, (8 * pins), GFP_KERNEL);
		if (!namestrings)
			return -ENOMEM;

	gpio->pfc = rza1pinctrl;
	gpio->port = port;
	gpio->npins = pins;

	for(i = 0; i < pins; i++){
		sprintf(namestrings, "p%u-%u", port, i);
		names[i] = namestrings;
		namestrings += 8;
	}

	gpiochip->label = ofnode->name;
	gpiochip->names = names;
	gpiochip->base = port * 16;
	gpiochip->ngpio = pins;
	gpiochip->get = chip_gpio_get;
	gpiochip->set = chip_gpio_set;
	gpiochip->direction_input = chip_direction_input;
	gpiochip->direction_output = chip_direction_output;
	gpiochip->parent = &pdev->dev;
	gpiochip->owner = THIS_MODULE;
	gpiochip->of_node = ofnode;

	retval = gpiochip_add_data(gpiochip, gpio);

	if (retval)
		dev_err(&pdev->dev, "Failed to register GPIO for port %u %d\n", port, retval);

	return retval;
}

static int rza1pfc_pinctrl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct property *prop;

	struct rza1pfc *rza1pinctrl;
	struct pinctrl_pin_desc *rza1_pins;
	struct resource *base_res;
	void __iomem *base;
	int i, j, retval, length;
	const __be32 *p;
	u32 val;

	int port, npins, curpin;

	/* Get io base address */
	base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!base_res)
		return -ENODEV;

	base = devm_ioremap_resource(&pdev->dev, base_res);
	if (IS_ERR(base))
		return PTR_ERR(rza1pinctrl->base);

	rza1pinctrl = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl), GFP_KERNEL);
	if (!rza1pinctrl)
		return -ENOMEM;

	rza1_pins = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl) * 168, GFP_KERNEL);
		if (!rza1pinctrl)
			return -ENOMEM;

	rza1pinctrl->base = base;
	mutex_init(&rza1pinctrl->mutex);

	rza1pinctrl->pctl_desc.name = dev_name(&pdev->dev);
	rza1pinctrl->pctl_desc.owner = THIS_MODULE;
	rza1pinctrl->pctl_desc.pins = rza1_pins;
	rza1pinctrl->pctl_desc.npins = 168;
	rza1pinctrl->pctl_desc.confops = &rza1pfc_pconf_ops;
	rza1pinctrl->pctl_desc.pctlops = &rza1pfc_pctrl_ops;
	rza1pinctrl->pctl_desc.pmxops = &rza1pfc_pmx_ops;
	rza1pinctrl->dev = &pdev->dev;


	// populate functions
	rza1pinctrl->nfunctions = 0;

	// find how many function blocks there are
	for_each_child_of_node(np, child) {
		dev_info(&pdev->dev, "child %s\n", child->name);
		prop = of_find_property(child, "renesas,pins", &length);
		if(prop)
			rza1pinctrl->nfunctions++;
	}

	rza1pinctrl->functions = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl->functions) * rza1pinctrl->nfunctions, GFP_KERNEL);
	i = 0;
	for_each_child_of_node(np, child) {
		rza1pinctrl->functions[i].name = child->name;

		// check if this is a function block
		prop = of_find_property(child, "renesas,pins", &length);
		if(!prop)
			continue;

		rza1pinctrl->functions[i].npins = (length / 4) / 3;

		dev_info(&pdev->dev, "function has %d pins\n", rza1pinctrl->functions[i].npins);

		rza1pinctrl->functions[i].pins = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl->functions->pins) *
				rza1pinctrl->functions[i].npins, GFP_KERNEL);
		rza1pinctrl->functions[i].modes = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl->functions->modes) *
						rza1pinctrl->functions[i].npins, GFP_KERNEL);
		rza1pinctrl->functions[i].dirs = devm_kzalloc(&pdev->dev, sizeof(*rza1pinctrl->functions->dirs) *
						rza1pinctrl->functions[i].npins, GFP_KERNEL);

		p = NULL;
		for(j = 0; j < rza1pinctrl->functions[i].npins; j++){
			p = of_prop_next_u32(prop, p, &val);
			rza1pinctrl->functions[i].pins[j] = val;
			p = of_prop_next_u32(prop, p, &val);
			rza1pinctrl->functions[i].modes[j] = val;
			p = of_prop_next_u32(prop, p, &val);
			rza1pinctrl->functions[i].dirs[j] = val;
			dev_info(&pdev->dev, "pin %u mode: %i dir: %i\n", rza1pinctrl->functions[i].pins[j],
					rza1pinctrl->functions[i].modes[j], rza1pinctrl->functions[i].dirs[j]);
		}
		i++;
	}
	//

	// find and register gpios
	curpin = 0;
	for_each_child_of_node(np, child) {
			prop = of_find_property(child, "gpio-controller", &length);
			if(prop){
				if(of_property_read_u32(child, "renesas,port", &port))
					continue;
				if(of_property_read_u32(child, "ngpios", &npins))
					continue;
				rza1pfc_gpio_registerport(pdev, child, rza1pinctrl, port, npins);

				for(i = 0; i < npins; i++){
					rza1_pins[curpin].number = RZA1PFC_PIN(port, i);
					curpin++;
				}
			}
	}
	//

	rza1pinctrl->pctl_dev = devm_pinctrl_register(&pdev->dev, &rza1pinctrl->pctl_desc,
						       rza1pinctrl);
	if (IS_ERR(rza1pinctrl->pctl_dev)) {
		dev_err(&pdev->dev, "Failed pinctrl registration\n");
		retval = PTR_ERR(rza1pinctrl->pctl_dev);
	}

	/*
	rza1pinctrl->gpio_range.name = name;
	rza1pinctrl->gpio_range.id = 0;
	rza1pinctrl->gpio_range.base = gpiochip->base;
	rza1pinctrl->gpio_range.pin_base = 0;
	rza1pinctrl->gpio_range.npins = gpiochip->ngpio;
	rza1pinctrl->gpio_range.gc = gpiochip;

	pinctrl_add_gpio_range(rza1pinctrl->pctl_dev,
					&rza1pinctrl->gpio_range);*/

	return retval;
}

static const struct of_device_id rza1_pinctrl_of_match[] = {
	{
			.compatible = "renesas,rza1-pinctrl",
	},
	{}
};

static struct platform_driver rza1_pinctrl_driver = {
	.driver = {
		.name = "pinctrl-rza1",
		.of_match_table = rza1_pinctrl_of_match,
	},
	.probe = rza1pfc_pinctrl_probe,
};

static struct platform_driver * const drivers[] = {
	&rza1_pinctrl_driver,
};

static int __init rza1_module_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

subsys_initcall(rza1_module_init);
