/*
 * RZ/A1 Timer Driver - OSTM
 *
 * Copyright (C) 2014 Renesas Solutions Corp.
 *
 * Based on drivers/clocksource/sh_mtu2.c
 *  Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sched_clock.h>

/*
 * [TODO] This driver doesn't support Power Management.
 */

struct rza1_ostm_clk {
	int irq;
	struct clk *clk;
	unsigned long rate;
	void __iomem *base;
};

struct rza1_ostm_clkevt {
	int mode;
	unsigned long ticks_per_jiffy;
	struct clock_event_device evt;
	struct irqaction irqaction;
};

struct rza1_ostm_priv {
	struct platform_device* pdev;
	struct rza1_ostm_clk	clk[2];
	struct rza1_ostm_clkevt clkevt;
};

static struct rza1_ostm_priv *rza1_ostm_priv;
static void __iomem *system_clock;

/* OSTM REGISTERS */
#define	OSTM_CMP		0x000	/* RW,32 */
#define	OSTM_CNT		0x004	/* R,32 */
#define	OSTM_TE			0x010	/* R,8 */
#define	OSTM_TS			0x014	/* W,8 */
#define	OSTM_TT			0x018	/* W,8 */
#define	OSTM_CTL		0x020	/* RW,8 */

#define	TE			0x01
#define	TS			0x01
#define	TT			0x01
#define	CTL_PERIODIC		0x00
#define	CTL_ONESHOT		0x02
#define	CTL_FREERUN		0x02

static int __init rza1_ostm_init_clk(struct device_node *node, struct rza1_ostm_priv *priv, int index){
	void *regs;
	struct clk *clk;
	int ret;

	regs = of_iomap(node, index);
	if (IS_ERR(regs)) {
		dev_err(&priv->pdev->dev, "failed to get I/O memory\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = of_irq_get(node, index);
	if (ret < 0) {
		dev_err(&priv->pdev->dev, "failed to get irq\n");
		goto err;
	}
	priv->clk[index].irq = ret;

	clk = of_clk_get(node, index);
	if (IS_ERR(clk)) {
		dev_err(&priv->pdev->dev, "failed to get clock\n");
		ret = -EINVAL;
		goto err;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&priv->pdev->dev, "failed to prepare clock enable %d\n", ret);
		goto err;
	}

	ret = clk_enable(clk);
	if (ret) {
		dev_err(&priv->pdev->dev, "failed to enable clock %d\n", ret);
		goto err;
	}

	priv->clk[index].clk = clk;
	priv->clk[index].base = regs;
	priv->clk[index].rate = clk_get_rate(clk);

err:
	return ret;
}


/*********************************************************************/
/*
 * Setup clock-source device (which is ostm.0)
 * as free-running mode.
 */
static int __init rza1_ostm_init_clksrc(struct device_node *node, struct rza1_ostm_priv *priv)
{
	int ret;
	struct rza1_ostm_clk *cs = &priv->clk[0];
	unsigned long rating = 300;
	ret = rza1_ostm_init_clk(node, priv, 0);
	if(!ret) {
		if (ioread8(cs->base + OSTM_TE) & TE) {
			iowrite8(TT, cs->base + OSTM_TT);
			while (ioread8(cs->base + OSTM_TE) & TE)
				;
		}
		iowrite32(0, cs->base + OSTM_CMP);
		iowrite8(CTL_FREERUN, cs->base + OSTM_CTL);
		iowrite8(TS, cs->base + OSTM_TS);

		clocksource_mmio_init(cs->base + OSTM_CNT,
				"ostm_clksrc", cs->rate,
				rating, 32, clocksource_mmio_readl_up);
	}
	return ret;
}

/*********************************************************************/
/*
 * Setup sched_clock using clocksource device.
 */
static u64 notrace rza1_ostm_read_sched_clock(void)
{
	return ioread32(system_clock);
}

static int __init rza1_ostm_init_sched_clock(struct rza1_ostm_clk *cs)
{
	unsigned long flags;

	system_clock = cs->base + OSTM_CNT;
	local_irq_save(flags);
	local_irq_disable();
	sched_clock_register(rza1_ostm_read_sched_clock, 32, cs->rate);
	local_irq_restore(flags);
	return 0;
}

/*********************************************************************/
/*
 * Setup clock-event device (which is ostm.1)
 * (interrupt-driven).
 */
static void rza1_ostm_clkevt_timer_stop(struct rza1_ostm_clk *clk)
{
	if (ioread8(clk->base + OSTM_TE) & TE) {
		iowrite8(TT, clk->base + OSTM_TT);
		while (ioread8(clk->base + OSTM_TE) & TE)
			;
	}
}

static int rza1_ostm_clkevt_set_next_event(unsigned long delta,
		struct clock_event_device *evt)
{
	struct rza1_ostm_clk *clk = &rza1_ostm_priv->clk[1];

	rza1_ostm_clkevt_timer_stop(clk);

	iowrite32(delta, clk->base + OSTM_CMP);
	iowrite8(CTL_ONESHOT, clk->base + OSTM_CTL);
	iowrite8(TS, clk->base + OSTM_TS);

	return 0;
}

static int rza1_ostm_set_state_shutdown(struct clock_event_device *evt){
	struct rza1_ostm_clk *clk = &rza1_ostm_priv->clk[1];
	struct rza1_ostm_clkevt *clkevt = &rza1_ostm_priv->clkevt;
	rza1_ostm_clkevt_timer_stop(clk);
	clkevt->mode = CLOCK_EVT_STATE_SHUTDOWN;
	return 0;
}

static int rza1_ostm_set_state_oneshot(struct clock_event_device *evt){
	struct rza1_ostm_clk *clk = &rza1_ostm_priv->clk[1];
	struct rza1_ostm_clkevt *clkevt = &rza1_ostm_priv->clkevt;
	rza1_ostm_clkevt_timer_stop(clk);
	clkevt->mode = CLOCK_EVT_STATE_ONESHOT;
	return 0;
}

static int rza1_ostm_set_state_periodic(struct clock_event_device *evt){
	struct rza1_ostm_clk *clk = &rza1_ostm_priv->clk[1];
	struct rza1_ostm_clkevt *clkevt = &rza1_ostm_priv->clkevt;
	iowrite32(clkevt->ticks_per_jiffy - 1, clk->base + OSTM_CMP);
	iowrite8(CTL_PERIODIC, clk->base + OSTM_CTL);
	iowrite8(TS, clk->base + OSTM_TS);
	clkevt->mode = CLOCK_EVT_STATE_PERIODIC;
	return 0;
}

static irqreturn_t rza1_ostm_timer_interrupt(int irq, void *dev_id)
{
#if 1
	struct rza1_ostm_priv *priv = dev_id;
	struct rza1_ostm_clk *clk = &priv->clk[1];
	struct rza1_ostm_clkevt *clkevt = &priv->clkevt;

	if (clkevt->mode == CLOCK_EVT_STATE_ONESHOT)
		rza1_ostm_clkevt_timer_stop(clk);

	if (clkevt->evt.event_handler)
		clkevt->evt.event_handler(&clkevt->evt);
	return IRQ_HANDLED;
#else
	struct clock_event_device *evt = dev_id;
	struct rza1_ostm_clkevt *clkevt;

	clkevt = container_of(evt, struct rza1_ostm_clkevt, evt);
	if (clkevt->mode == CLOCK_EVT_STATE_ONESHOT)
		rza1_ostm_clkevt_timer_stop(clkevt);

	if (evt->event_handler)
		evt->event_handler(evt);
	return IRQ_HANDLED;
#endif
}

static int __init rza1_ostm_init_clkevt(struct device_node *node, struct rza1_ostm_priv *priv)
{
	struct rza1_ostm_clk *clk = &priv->clk[1];
	struct rza1_ostm_clkevt *ce;
	struct clock_event_device *evt;
	unsigned long rating = 300;
	int ret;

	ret = rza1_ostm_init_clk(node, priv, 1);
	if(!ret){
		ce = &priv->clkevt;

		ce->ticks_per_jiffy = (clk->rate + HZ / 2) / HZ;
		ce->mode = CLOCK_EVT_STATE_DETACHED;

		ce->irqaction.name = "ostm.1";
		ce->irqaction.handler = rza1_ostm_timer_interrupt;
		ce->irqaction.dev_id = priv;
		ce->irqaction.irq = clk->irq;
		ce->irqaction.flags = IRQF_TRIGGER_RISING | IRQF_TIMER;
		ret = setup_irq(clk->irq, &ce->irqaction);


		//ret = devm_request_irq(&priv->pdev->dev, clk->irq,
		//		rza1_ostm_timer_interrupt, IRQF_TRIGGER_RISING | IRQF_TIMER, "ostm.1", priv);

		if (ret) {
			dev_err(&priv->pdev->dev, "failed to request irq\n");
		}

		else {
			evt = &ce->evt;
			evt->name = "ostm";
			evt->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC;

			evt->set_state_shutdown = rza1_ostm_set_state_shutdown;
			evt->set_state_oneshot = rza1_ostm_set_state_oneshot;
			evt->set_state_periodic = rza1_ostm_set_state_periodic;

			evt->set_next_event = rza1_ostm_clkevt_set_next_event;
			evt->shift = 32;
			evt->rating = rating;
			evt->cpumask = cpumask_of(0);
			clockevents_config_and_register(evt, clk->rate, 0xf, 0xffffffff);
		}
	}
	return ret;
}

static int __init rza1_ostm_init(struct device_node *node)
{
	struct rza1_ostm_priv *priv;
	struct platform_device* pdev = of_find_device_by_node(node);
	int ret = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_info(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	priv->pdev = pdev;
	rza1_ostm_priv = priv;


	ret = rza1_ostm_init_clksrc(node, priv);
	if (ret)
		goto err;

	ret = rza1_ostm_init_sched_clock(&priv->clk[0]);
	if (ret)
		goto err;

	ret = rza1_ostm_init_clkevt(node, priv);

	if (ret) {
		kfree(priv);
		//pm_runtime_idle(&pdev->dev);
		return ret;
	}

err:


out:
	//if (pdata.clksrc.rating || pdata.clkevt.rating)
	//	pm_runtime_irq_safe(&pdev->dev);
	//else
	//	pm_runtime_idle(&pdev->dev);

	return ret;
}

CLOCKSOURCE_OF_DECLARE(ostm, "renesas,sh-ostm",
		rza1_ostm_init);

