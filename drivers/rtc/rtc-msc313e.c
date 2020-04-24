/*
 * Real Time Clock driver for msb252x
 *
 * (C) 2011 Heyn lu, Mstar
 * (C) 2019 Daniel Palmer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/ctype.h>
#include <asm/io.h>
#include <linux/of_irq.h>

#define DRIVER_NAME	"msc313e-rtc"

#define REG_RTC_CTRL        0x00
    #define SOFT_RSTZ_BIT   BIT(0)
    #define CNT_EN_BIT      BIT(1)
    #define WRAP_EN_BIT     BIT(2)
    #define LOAD_EN_BIT     BIT(3)
    #define READ_EN_BIT     BIT(4)
    #define INT_MASK_BIT    BIT(5)
    #define INT_FORCE_BIT   BIT(6)
    #define INT_CLEAR_BIT   BIT(7)

#define REG_RTC_FREQ_CW_L   0x04
#define REG_RTC_FREQ_CW_H   0x08

#define REG_RTC_LOAD_VAL_L  0x0C
#define REG_RTC_LOAD_VAL_H  0x10

#define REG_RTC_MATCH_VAL_L 0x14
#define REG_RTC_MATCH_VAL_H 0x18

#define REG_RTC_CNT_VAL_L   0x20
#define REG_RTC_CNT_VAL_H   0x24

struct ms_rtc_info {
    struct platform_device *pdev;
    struct rtc_device *rtc_dev;
    void __iomem *rtc_base;
};

int auto_wakeup_delay_seconds = 0;

static ssize_t auto_wakeup_timer_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t n)
{
    if(NULL!=buf)
    {
        size_t len;
        const char *str = buf;
        while (*str && !isspace(*str)) str++;
        len = str - buf;
        if(len)
        {
            auto_wakeup_delay_seconds = simple_strtoul(buf, NULL, 10);
            //printk("\nauto_wakeup_delay_seconds=%d\n", auto_wakeup_delay_seconds);
            return n;
        }
        return -EINVAL;
    }
    return -EINVAL;
}
static ssize_t auto_wakeup_timer_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    char *str = buf;
    char *end = buf + PAGE_SIZE;

    str += scnprintf(str, end - str, "%d\n", auto_wakeup_delay_seconds);
    return (str - buf);
}
DEVICE_ATTR(auto_wakeup_timer, 0644, auto_wakeup_timer_show, auto_wakeup_timer_store);


static int ms_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
    struct ms_rtc_info *info = dev_get_drvdata(dev);
    unsigned long seconds;

    seconds = readw(info->rtc_base + REG_RTC_MATCH_VAL_L) | (readw(info->rtc_base + REG_RTC_MATCH_VAL_H) << 16);

    rtc_time_to_tm(seconds, &alarm->time);

    if( !(readw(info->rtc_base + REG_RTC_CTRL) & INT_MASK_BIT) )
        alarm->enabled = 1;

    return 0;
}

static int ms_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
    struct ms_rtc_info *info = dev_get_drvdata(dev);
    unsigned long seconds;
    u16 reg;

    rtc_tm_to_time(&alarm->time, &seconds);

    writew((seconds & 0xFFFF), info->rtc_base + REG_RTC_MATCH_VAL_L);
    writew((seconds>>16) & 0xFFFF, info->rtc_base + REG_RTC_MATCH_VAL_H);

    reg = readw(info->rtc_base + REG_RTC_CTRL);
    if(alarm->enabled)
    {
        writew(reg & ~(INT_MASK_BIT), info->rtc_base + REG_RTC_CTRL);
    }
    else
    {
        writew(reg | INT_MASK_BIT, info->rtc_base + REG_RTC_CTRL);
    }

    return 0;
}

static int ms_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
    struct ms_rtc_info *info = dev_get_drvdata(dev);
    unsigned long seconds;
    u16 reg;

    reg = readw(info->rtc_base + REG_RTC_CTRL);
    writew(reg | READ_EN_BIT, info->rtc_base + REG_RTC_CTRL);
    while(readw(info->rtc_base + REG_RTC_CTRL) & READ_EN_BIT);  //wait for HW latch done

    seconds = readw(info->rtc_base + REG_RTC_CNT_VAL_L) | (readw(info->rtc_base + REG_RTC_CNT_VAL_H) << 16);

    rtc_time_to_tm(seconds, tm);

    return rtc_valid_tm(tm);
}

static int ms_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
    struct ms_rtc_info *info = dev_get_drvdata(dev);
    unsigned long seconds;
    u16 reg;

    rtc_tm_to_time(tm, &seconds);
    writew(seconds & 0xFFFF, info->rtc_base + REG_RTC_LOAD_VAL_L);
    writew((seconds >> 16) & 0xFFFF, info->rtc_base + REG_RTC_LOAD_VAL_H);
    reg = readw(info->rtc_base + REG_RTC_CTRL);
    writew(reg | LOAD_EN_BIT, info->rtc_base + REG_RTC_CTRL);
    /* need to check carefully if we want to clear REG_RTC_LOAD_VAL_H for customer*/
    while(readw(info->rtc_base + REG_RTC_CTRL) & LOAD_EN_BIT);
    writew(0, info->rtc_base + REG_RTC_LOAD_VAL_H);

    return 0;
}

static const struct rtc_class_ops ms_rtc_ops = {
    .read_time = ms_rtc_read_time,
    .set_time = ms_rtc_set_time,
    .read_alarm = ms_rtc_read_alarm,
    .set_alarm = ms_rtc_set_alarm,
};

static irqreturn_t ms_rtc_interrupt(s32 irq, void *dev_id)
{
    struct ms_rtc_info *info = dev_get_drvdata(dev_id);
    u16 reg;

    reg = readw(info->rtc_base + REG_RTC_CTRL);
    reg |= INT_CLEAR_BIT;
    writew(reg, info->rtc_base + REG_RTC_CTRL);

    return IRQ_HANDLED;
}


#ifdef CONFIG_PM
static s32 ms_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
    if(auto_wakeup_delay_seconds)
    {
        struct rtc_time tm;
        struct rtc_wkalrm alarm;
        unsigned long seconds;
        ms_rtc_read_time(&pdev->dev, &tm);
        rtc_tm_to_time(&tm, &seconds);
        seconds += auto_wakeup_delay_seconds;
        rtc_time_to_tm(seconds, &alarm.time);
        alarm.enabled=1;
        ms_rtc_set_alarm(&pdev->dev, &alarm);
    }
    return 0;
}

static s32 ms_rtc_resume(struct platform_device *pdev)
{
    return 0;
}
#endif

static int ms_rtc_remove(struct platform_device *pdev)
{
    struct clk *clk = of_clk_get(pdev->dev.of_node, 0);

    if(IS_ERR(clk))
    {
        return PTR_ERR(clk);
    }
    clk_disable_unprepare(clk);

    return 0;
}

static int ms_rtc_probe(struct platform_device *pdev)
{
    struct ms_rtc_info *info;
    struct resource *res;
    struct clk *clk;
    dev_t dev;
    int ret = 0, irq;
    u16 reg;
    u32 rate;

    info = devm_kzalloc(&pdev->dev, sizeof(struct ms_rtc_info), GFP_KERNEL);
    if (!info)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
    {
		return -ENODEV;
	}
    info->rtc_base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(info->rtc_base))
        return PTR_ERR(info->rtc_base);
    info->pdev = pdev;

    res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res)
    {
		return -ENODEV;
	}

	irq = of_irq_get(pdev->dev.of_node, 0);
	if(irq == 0)
	{
		return -ENODEV;
	}
	ret = devm_request_irq(&pdev->dev, irq, ms_rtc_interrupt, IRQF_SHARED,
			dev_name(&pdev->dev), &pdev->dev);
	if (ret)
		return ret;

    platform_set_drvdata(pdev, info);

    info->rtc_dev = devm_rtc_device_register(&pdev->dev,
                dev_name(&pdev->dev), &ms_rtc_ops,
                THIS_MODULE);

    if (IS_ERR(info->rtc_dev)) {
        return PTR_ERR(info->rtc_dev);
    }

    //Note: is it needed?
    //device_set_wakeup_capable(&pdev->dev, 1);
    //device_wakeup_enable(&pdev->dev);

    //init rtc
    //1. release reset
    reg = readw(info->rtc_base + REG_RTC_CTRL);
    if( !(reg & SOFT_RSTZ_BIT) )
    {
        reg |= SOFT_RSTZ_BIT;
        writew(reg, info->rtc_base + REG_RTC_CTRL);
    }
    //2. set frequency
    clk = of_clk_get(pdev->dev.of_node, 0);
    if(IS_ERR(clk))
    {
        return PTR_ERR(clk);
    }

    /* Try to determine the frequency from the device tree */
    if (of_property_read_u32(pdev->dev.of_node, "clock-frequency", &rate))
    {
    	rate = clk_get_rate(clk);
    }
    else
    {
        clk_set_rate(clk, rate);
    }

    clk_prepare_enable(clk);
    writew(rate & 0xFFFF, info->rtc_base + REG_RTC_FREQ_CW_L);
    writew((rate >>16)  & 0xFFFF, info->rtc_base + REG_RTC_FREQ_CW_H);

    //3. enable counter
    reg |= CNT_EN_BIT;
    writew(reg, info->rtc_base + REG_RTC_CTRL);

    if (0 != (ret = alloc_chrdev_region(&dev, 0, 1, DRIVER_NAME)))
        return ret;

    return ret;
}

static const struct of_device_id ms_rtc_of_match_table[] = {
    { .compatible = "mstar,msc313e-rtc" },
    {}
};
MODULE_DEVICE_TABLE(of, ms_rtc_of_match_table);

static struct platform_driver ms_rtc_driver = {
    .remove = ms_rtc_remove,
    .probe = ms_rtc_probe,
#ifdef CONFIG_PM
    .suspend = ms_rtc_suspend,
    .resume = ms_rtc_resume,
#endif
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = ms_rtc_of_match_table,
    },
};

module_platform_driver(ms_rtc_driver);

MODULE_AUTHOR("MStar Semiconductor, Inc.");
MODULE_DESCRIPTION("MStar RTC Driver");
MODULE_LICENSE("GPL v2");
