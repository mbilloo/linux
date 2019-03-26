#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <asm-generic/io.h>

/*
 * 0x0	- ctrl
 *    4    |   3   |  1   |  0
 * capture | clear | trig |  en
 * 
 * 0x08 - max low
 * 0x0c - max high
 * 0x10 - counter low word
 * 0x14 - counter high word
 * 
 */

#define DRIVER_NAME "msc313e-timer"
#define REG_COUNTER_LOW  0x10
#define REG_COUNTER_HIGH 0x14

struct msc313e_timer {
	__iomem void* base;
	struct clk *clk;
	struct clocksource clksrc;
};
#define to_msc313e_timer(ptr) container_of(ptr, struct msc313e_timer, clksrc)

static const struct of_device_id msc313e_timer_dt_ids[] = {
	{ .compatible = "mstar,msc313e-timer" },
	{},
};

MODULE_DEVICE_TABLE(of, msc313e_timer_dt_ids);

static u64 msc313e_timer_read(struct clocksource *c)
{
	struct msc313e_timer *timer = to_msc313e_timer(c);
	return (ioread16(timer->base + REG_COUNTER_LOW) | (ioread16(timer->base + REG_COUNTER_HIGH) << 16)) & c->mask;
}

static int msc313e_timer_probe(struct platform_device *pdev)
{
	int ret;
	struct msc313e_timer *timer;
	struct resource *res;

	timer = devm_kzalloc(&pdev->dev, sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	timer->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(timer->base))
		return PTR_ERR(timer->base);
	
	timer->clk = of_clk_get(pdev->dev.of_node, 0);
	clk_prepare_enable(timer->clk);
	
	timer->clksrc.name = platform_get_device_id(pdev)->name;
	timer->clksrc.rating = 200;
	timer->clksrc.read = msc313e_timer_read;
	timer->clksrc.mask = CLOCKSOURCE_MASK(32);
	timer->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	
	return clocksource_register_hz(&timer->clksrc, clk_get_rate(timer->clk));
}

static int msc313e_timer_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_timer_driver = {
	.probe = msc313e_timer_probe,
	.remove = msc313e_timer_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = msc313e_timer_dt_ids,
	},
};

module_platform_driver(msc313e_timer_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mstar MSC313e PWM driver");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");