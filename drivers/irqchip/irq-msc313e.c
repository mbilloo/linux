// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

/*
 *
 * GIC 32 - 95 - hardware interrupts that go through the intc before the gic
 * Seems to go from 350 - > 3cf
 * mask bits
 * 54 0 - 15
 * 55 16 - 31
 * 56 31 - 47
 * 57 48 - 63
 *
 * interrupt polarity for the 64 gic hw interrupts are in registers
 * 58 0 - 15
 * 59 16 - 31
 * 5a 31 - 47
 * 5b 48 - 63
 *
 * raw interrupt status bits
 *
 * 5c
 * 5d
 * 5e
 * 5f
 *
 *
 * GIC 96 - 127 - hardware fiq that go through the intc before the gic
 * mask bits
 * 44 0  - 15
 * 45 16 - 31
 * 48 0 - 15
 * 49 16 - 31
 *
 * clear bits
 * 4c
 * 4d
 * 4e
 * 4f
 */

#define NR_INTR_SLEEP	32

struct msc313e_intc {
	void __iomem *base;
	u8 gicoff;
	u8 interrupts;
};

static void msc313e_intc_maskunmask_hw_int(struct msc313e_intc *intc, int hwirq, bool mask){
	int bitoff = hwirq % 16;
	int regoff = (hwirq >> 4) * 4;
	__iomem void* addr = intc->base + regoff;
	u16 reg = ioread16(addr);

	if (mask) {
		reg |= 1 << bitoff;
	} else {
		reg &= ~(1 << bitoff);
	}
	iowrite16(reg, addr);
}

static void msc313e_intc_mask_irq(struct irq_data *data)
{
	struct msc313e_intc* intc = data->chip_data;
	msc313e_intc_maskunmask_hw_int(intc, data->hwirq, true);
	irq_chip_mask_parent(data);
}

static void msc313e_intc_unmask_irq(struct irq_data *data)
{
	struct msc313e_intc* intc = data->chip_data;
	msc313e_intc_maskunmask_hw_int(intc, data->hwirq, false);
	irq_chip_unmask_parent(data);
}

static int  msc313e_intc_set_type_irq(struct irq_data *data, unsigned int flow_type)
{
	struct msc313e_intc* intc = data->chip_data;
	int irq = data->hwirq;
	int bitoff = irq % 16;
	int regoff = (irq >> 4) * 4;
	__iomem void* addr = intc->base + 16 + regoff;
	u16 reg = ioread16(addr);

	if (flow_type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_HIGH)) {
		reg &= ~(1 << bitoff);
	} else {
		reg |= 1 << bitoff;
	}

	iowrite16(reg, addr);
    return 0;
}

static struct irq_chip msc313e_intc_chip = {
	.name			= "INTC",
	.irq_mask		= msc313e_intc_mask_irq,
	.irq_unmask		= msc313e_intc_unmask_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= msc313e_intc_set_type_irq,
};

static int msc313e_intc_domain_translate(struct irq_domain *d,
				     struct irq_fwspec *fwspec,
				     unsigned long *hwirq,
				     unsigned int *type)
{
	printk("msc313e-intc: domain_translate");
	if (!is_of_node(fwspec->fwnode) || fwspec->param_count != 2)
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];

	return 0;
}

static int msc313e_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct msc313e_intc* intc = domain->host_data;

	printk("msc313e-intc: domain_alloc");

	if (fwspec->param_count != 2)
		return -EINVAL;

	irq_domain_set_hwirq_and_chip(domain, virq, fwspec->param[0], &msc313e_intc_chip, intc);

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param[0] = GIC_SPI;
	parent_fwspec.param[1] = fwspec->param[0] + 32;
	parent_fwspec.param[2] = fwspec->param[1];
	parent_fwspec.param_count = 3;
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops msc313e_intc_domain_ops = {
		.translate = msc313e_intc_domain_translate,
		.alloc = msc313e_intc_domain_alloc,
		.free = irq_domain_free_irqs_common,
};

static int __init msc313e_intc_of_init(struct device_node *node,
				   struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	struct msc313e_intc *intc;

	printk("msc313e-intc: init\n");

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("msc313e-intc: interrupt-parent not found\n");
		return -EINVAL;
	}

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	intc->base = of_iomap(node, 0);
	if (IS_ERR(intc->base))
		return PTR_ERR(intc->base);

	/*if(!of_property_read_u8(node, "gic-offset", &intc-gicoff)){
		pr_err("msc313e-intc: gic offset must be specified");
		return -ENODEV;
	}*/
	intc->gicoff = 32;

	/*if(!of_property_read_u32(node, "nr-interrupts", &intc->interrupts)){
		("msc313e-intc: number of interrupts must be specified");
		return -ENODEV;
	}*/
	intc->interrupts = 64;

	domain = irq_domain_add_hierarchy(domain_parent, 0, intc->interrupts, node,
			&msc313e_intc_domain_ops, intc);
	if (!domain) {
		pr_err("msc313e-intc: failed to add irq domain\n");
		return -ENOMEM;
	}

	printk("msc313e-intc: done\n");

	return 0;
}

IRQCHIP_DECLARE(mstar_msc313e_intc, "mstar,msc313e-intc",
		msc313e_intc_of_init);
