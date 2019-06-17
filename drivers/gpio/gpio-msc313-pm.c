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
#include <linux/of_irq.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/irqchip.h>

#define DRIVER_NAME			"gpio-msc313-pm"

/*
 * MSC313e pm gpio
 *
 * - A gpio block in the "pm" area at 0x1f001E00
 *
 * 0x10 - gpio4
 * 0x60	- spi_cz
 *
 * 0x110 - spi_wp
 * 0x11c - sd_cdz
 *
 * 15 - 12 | 11 - 0 |      9       |    8       |    7     |    6    | 5 |    4     | 3 | 2  |  1  |  0
 *    ?    |    0   | INVERTED IN? | INT STATUS | INT TYPE | INT CLR | ? | INT MASK | ? | IN | OUT | OEN
 *         |        |     ro?      |   ro?      |          |   wo    |   |          |   |    |     |
 *
 * bit 9 reacts to the pin being pulled up and down
 *
 * Reset value is 0x0215
 *
 */

#define BIT_OEN			BIT(0)
#define BIT_OUT			BIT(1)
#define BIT_IN			BIT(2)
#define BIT_IRQ_MASK	BIT(4)
#define BIT_IRQ_CLEAR	BIT(6)
#define BIT_IRQ_TYPE	BIT(7)

static const char *gpionames[] = {
		"pm_gpio4",
		"pm_sd_sdz"
};

static const int msc313e_pm_offsets[] = {
		0x10,
		0x11c
};

struct msc313e_pm_pinctrl {
	struct device *dev;
	void __iomem *base;
	struct fwnode_handle* fwnode;
};

static void msc313e_pm_pinctrl_irq_eoi(struct irq_data *data){
	__iomem void *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);
	reg |= BIT_IRQ_CLEAR;
	writew_relaxed(reg, addr);
	irq_chip_eoi_parent(data);
};

static void msc313e_pm_pinctrl_irq_mask(struct irq_data *data){
	__iomem void *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);
	reg |= BIT_IRQ_MASK;
	writew_relaxed(reg, addr);
	irq_chip_mask_parent(data);
};

static void msc313e_pm_pinctrl_irq_unmask(struct irq_data *data){
	__iomem void *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);
	reg &= ~BIT_IRQ_MASK;
	writew_relaxed(reg, addr);
	irq_chip_unmask_parent(data);
}

static int	msc313e_pm_pinctrl_irq_set_type(struct irq_data *data, unsigned int flow_type){
	__iomem void *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);
	if(flow_type)
		reg &= BIT_IRQ_TYPE;
	else
		reg |= BIT_IRQ_TYPE;

	writew_relaxed(reg, addr);
	return 0;
}

static struct irq_chip msc313e_pm_pinctrl_irqchip = {
	.name = "PM-GPIO",
	.irq_eoi = msc313e_pm_pinctrl_irq_eoi,
	.irq_mask = msc313e_pm_pinctrl_irq_mask,
	.irq_unmask = msc313e_pm_pinctrl_irq_unmask,
	.irq_set_type = msc313e_pm_pinctrl_irq_set_type,
};

static int msc313e_pm_pinctrl_domain_translate(struct irq_domain *d,
					struct irq_fwspec *fwspec,
					unsigned long *hwirq,
					unsigned int *type)
{
	if (!is_of_node(fwspec->fwnode) || fwspec->param_count != 3)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];

	return 0;
}

static int msc313e_pm_pinctrl_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct msc313e_pm_pinctrl* pinctrl = domain->host_data;
	struct irq_fwspec parent_fwspec;
	__iomem void* addr;

	if (fwspec->param_count != 3)
		return -EINVAL;

	addr = pinctrl->base + msc313e_pm_offsets[fwspec->param[0]];
	irq_domain_set_info(domain, virq, fwspec->param[0],
			&msc313e_pm_pinctrl_irqchip, addr, handle_fasteoi_irq, NULL, NULL);

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param[0] = fwspec->param[1];
	parent_fwspec.param[1] = fwspec->param[2];
	parent_fwspec.param_count = 2;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &parent_fwspec);
}

static const struct irq_domain_ops msc313e_pm_pinctrl_irq_domain_ops = {
		.translate = msc313e_pm_pinctrl_domain_translate,
		.alloc = msc313e_pm_pinctrl_irq_domain_alloc,
		.free = irq_domain_free_irqs_common,
};

static int msc313e_pm_pinctrl_irq_setup(struct platform_device *pdev, struct gpio_chip* gpiochip)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(gpiochip);
	struct irq_domain *domain, *parent_domain;
	struct device_node *parent_node;

	parent_node = of_irq_find_parent(pdev->dev.of_node);
	if (!parent_node)
		return -ENXIO;

	parent_domain = irq_find_host(parent_node);
	of_node_put(parent_node);
	if (!parent_domain)
		return -ENXIO;

	pinctrl->fwnode = of_node_to_fwnode(pdev->dev.of_node);
	domain = irq_domain_add_hierarchy(parent_domain, 0,
							    32,
								pdev->dev.of_node,
								&msc313e_pm_pinctrl_irq_domain_ops,
							    pinctrl);

	if (!domain)
		return -ENODEV;

	return 0;
}

static void msc313e_pm_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(chip);
	__iomem void *addr = pinctrl->base + msc313e_pm_offsets[offset];
	u16 reg = readw_relaxed(addr);
	if(value)
		reg |= BIT_OUT;
	else
		reg &= ~BIT_OUT;
	writew_relaxed(reg, addr);
}

static int msc313e_pm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(chip);
	u16 reg = readw_relaxed(pinctrl->base + msc313e_pm_offsets[offset]);
	return reg & BIT_IN ? 1 : 0;
}

static int msc313e_pm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(chip);
	__iomem void *addr = pinctrl->base + msc313e_pm_offsets[offset];
	u16 reg = readw_relaxed(addr);
	reg |= BIT_OEN;
	writew_relaxed(reg, addr);
	return 0;
}

static int msc313e_pm_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(chip);
	__iomem void *addr = pinctrl->base + msc313e_pm_offsets[offset];
	u16 reg = readw_relaxed(addr);
	reg &= ~BIT_OEN;
	writew_relaxed(reg, addr);
	return 0;
}

static int msc313e_pm_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_pm_pinctrl *pinctrl = gpiochip_get_data(chip);
	return of_irq_get_byname(pinctrl->dev->of_node, gpionames[offset]);
}

static int msc313e_pm_pinctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct msc313e_pm_pinctrl *pinctrl;
	struct resource *res;
	struct gpio_chip* gpiochip;

	dev_info(&pdev->dev, "msc313e pm pinctrl probe");

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	pinctrl->dev = &pdev->dev;

	platform_set_drvdata(pdev, pinctrl);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pinctrl->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->base))
		return PTR_ERR(pinctrl->base);

	gpiochip = devm_kzalloc(&pdev->dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpiochip->label            = DRIVER_NAME;
	gpiochip->parent		   = &pdev->dev;
	gpiochip->request          = gpiochip_generic_request;
	gpiochip->free             = gpiochip_generic_free;
	gpiochip->direction_input  = msc313e_pm_gpio_direction_input;
	gpiochip->get              = msc313e_pm_gpio_get;
	gpiochip->direction_output = msc313e_pm_gpio_direction_output;
	gpiochip->set              = msc313e_pm_gpio_set;
	gpiochip->to_irq		   = msc313e_pm_gpio_to_irq;
	gpiochip->base             = -1;
	gpiochip->ngpio            = ARRAY_SIZE(msc313e_pm_offsets);
	gpiochip->names            = gpionames;

	ret = gpiochip_add_data(gpiochip, pinctrl);
	if (ret < 0) {
		dev_err(pinctrl->dev,"failed to register gpio chip\n");
		return ret;
	}

	ret = msc313e_pm_pinctrl_irq_setup(pdev, gpiochip);
	if(ret)
		goto out;

	dev_info(pinctrl->dev, "msc313e pm pinctrl done");

out:
	return ret;
}

static const struct of_device_id msc313e_pm_pinctrl_of_match[] = {
	{
		/* MSC313E PM */
		.compatible	= "mstar,msc313-gpio-pm",
	},
	{ }
};

static struct platform_driver msc313e_pm_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = msc313e_pm_pinctrl_of_match,
	},
	.probe = msc313e_pm_pinctrl_probe,
};

static int __init msc313e_pm_pinctrl_init(void)
{
	return platform_driver_register(&msc313e_pm_pinctrl_driver);
}
core_initcall(msc313e_pm_pinctrl_init);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("pm gpio controller driver for MStar MSC313e SoCs");
MODULE_LICENSE("GPL v2");
