// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

/*
 * sleep intc -> intc -> gic
*/

#define NR_INTR_SLEEP	32

#define REG_STATUS_LOW	0x0
#define REG_STATUS_HIGH	0x4

struct msc313e_sleep_intc {
	void __iomem *base;
	struct irq_domain *domain;
};

static struct irq_chip msc313e_pm_intc_chip = {
	.name			= "PM-INTC"
};

static void msc313e_sleep_intc_chainedhandler(struct irq_desc *desc){
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct msc313e_sleep_intc *intc = irq_desc_get_handler_data(desc);
	u32 status;
	unsigned int hwirq;
	unsigned int virq;

	chained_irq_enter(chip, desc);
	status = ioread16(intc->base + REG_STATUS_LOW);
	status |= ioread16(intc->base + REG_STATUS_HIGH) << 16;

	while (status) {
		hwirq = __ffs(status);
		virq = irq_find_mapping(intc->domain, hwirq);
		if (virq)
			generic_handle_irq(virq);
		status &= ~BIT(hwirq);
	}

	chained_irq_exit(chip, desc);
}

static int msc313e_pm_intc_domain_translate(struct irq_domain *d,
					struct irq_fwspec *fwspec,
					unsigned long *hwirq,
					unsigned int *type)
{
	if (!is_of_node(fwspec->fwnode) || fwspec->param_count != 2)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];
	return 0;
}

static int msc313e_pm_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct msc313e_intc* intc = domain->host_data;

	printk("msc313e-pm-intc: domain_alloc");

	if (fwspec->param_count != 2)
		return -EINVAL;

	irq_domain_set_hwirq_and_chip(domain, virq, fwspec->param[0], &msc313e_pm_intc_chip, intc);

	return 0;
}

static const struct irq_domain_ops msc313e_pm_intc_domain_ops = {
		.translate = msc313e_pm_intc_domain_translate,
		.alloc = msc313e_pm_intc_domain_alloc,
		.free = irq_domain_free_irqs_common,
};

static int __init msc313e_sleep_intc_of_init(struct device_node *node,
				   struct device_node *parent)
{
	int gicint;
	struct msc313e_sleep_intc *intc;

	printk("msc313e-pm-intc: init\n");

	gicint = of_irq_get(node, 0);
	if(gicint == 0){
		printk("you must specify the gic interrupt");
		return -ENODEV;
	}

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	intc->base = of_iomap(node, 0);
	if (IS_ERR(intc->base))
		return PTR_ERR(intc->base);

	printk("sleep irq is %d", gicint);
	irq_set_chained_handler_and_data(gicint, msc313e_sleep_intc_chainedhandler, intc);

	intc->domain = irq_domain_add_simple(node, 32, 0,
			&msc313e_pm_intc_domain_ops, intc);
	if (!intc->domain) {
		pr_err("msc313e-pm-intc: failed to add irq domain\n");
		return -ENOMEM;
	}

	printk("msc313e-pm-intc: done\n");

	return 0;
}

IRQCHIP_DECLARE(mstar_msc313e_sleep_intc, "mstar,msc313e-sleep-intc",
		msc313e_sleep_intc_of_init);
