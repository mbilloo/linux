/*
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/iio/iio.h>
#include <linux/gpio/driver.h>

/*
 * MSC313E SAR
 *
 * Seems to be 10bit?
 *
 * 0x0 - ctrl
 * 0x4 - sample period
 *
 * GPIO function are controlled via some bits in these registers:
 *
 * 0x44
 * CH?_EN bits control if the pin is an ADC pin or GPIO, 1 = ADC, 0 = GPIO
 * CH?_OEN bits control the gpio direction, 1 = input, 0 = output
 *
 *    11   |    10   |    9    |    8    | .. |    3   |    2   |    1   |    0
 * CH3_OEN | CH2_OEN | CH1_OEN | CH0_OEN | .. | CH3_EN | CH2_EN | CH1_EN | CH0_EN
 *
 * 0x48
 * CH?_IN bits represent the gpio input level
 * CH?_OUT bits set the gpio output level
 *
 *    11  |   10   |    9   |    8   | .. |    3    |    2    |    1    |    0
 * CH3_IN | CH2_IN | CH1_IN | CH0_IN | .. | CH3_OUT | CH2_OUT | CH1_OUT | CH0_OUT
 *
 * 0x50 - int mask
 * 0x54 - int clear
 *
 * 0x64 - vref sel
 *
 * 7 - 0
 *   ?
 */

#define DRIVER_NAME "msc313e-sar"

#define REG_CTRL			 0x0
#define REG_CTRL_TRIGGER	 BIT(14)
#define REG_SAMPLE_PERIOD	 0x4
#define REG_INT_CLR			 0x54

#define REG_GPIO_CTRL		0x44
#define REG_GPIO_DATA		0x48

struct msc313e_sar {
	__iomem void *base;
	struct clk *clk;
};

static const struct of_device_id msc313e_sar_dt_ids[] = {
	{ .compatible = "mstar,msc313e-sar" },
	{},
};
MODULE_DEVICE_TABLE(of, msc313e_sar_dt_ids);

static void msc313_sar_trigger(struct msc313e_sar *sar)
{
	u16 regval = ioread16(sar->base + REG_CTRL);
	regval |= REG_CTRL_TRIGGER;
	iowrite16(regval, sar->base + REG_CTRL);
}

static int msc313e_sar_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
		struct msc313e_sar *sar = iio_priv(indio_dev);
		u16 data;

		switch(mask){
		case IIO_CHAN_INFO_RAW:
			msc313_sar_trigger(sar);
			data = ioread16(sar->base + chan->address);
			*val = data;
			*val2 = 0;
			return IIO_VAL_INT;
		case IIO_CHAN_INFO_SCALE:
			*val = 3;
			*val2 = 0;
			return IIO_VAL_INT;
		}

		return 0;
};

static const struct iio_info msc313e_sar_iio_info = {
	.read_raw = msc313e_sar_read_raw,
};

#define MSC313E_SAR_CHAN_REG(ch) (0x100 + (ch * 4))
#define MSC313E_SAR_CHAN(index) \
	{ .type = IIO_VOLTAGE, \
	  .indexed = 1, \
	  .channel = index, \
	  .info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	  .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	  .address = MSC313E_SAR_CHAN_REG(index), \
	  .datasheet_name = "sar#index"\
	}

static const struct iio_chan_spec msc313e_sar_channels[] = {
		MSC313E_SAR_CHAN(0),
		MSC313E_SAR_CHAN(1),
		MSC313E_SAR_CHAN(2),
		MSC313E_SAR_CHAN(3),
};

static irqreturn_t msc313e_sar_irq(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct msc313e_sar *sar = iio_priv(indio_dev);
	iowrite16(0xffff, sar->base + REG_INT_CLR);
	return IRQ_HANDLED;
}

static int msc313e_sar_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static void msc313e_sar_gpio_free(struct gpio_chip *chip, unsigned offset)
{
}

static void msc313e_sar_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct msc313e_sar *sar = gpiochip_get_data(chip);
	u16 val = ioread16(sar->base + REG_GPIO_DATA);
	if(value)
		val |= BIT(offset);
	else
		val &= ~BIT(offset);
	iowrite16(val, sar->base + REG_GPIO_DATA);
}

static int msc313e_sar_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_sar *sar = gpiochip_get_data(chip);
	u16 val = ioread16(sar->base + REG_GPIO_DATA) >> (8 + offset);
	return val;
}

static int msc313e_sar_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_sar *sar = gpiochip_get_data(chip);
	u16 val = ioread16(sar->base + REG_GPIO_CTRL);
	val |= (BIT(offset) << 8);
	iowrite16(val, sar->base + REG_GPIO_CTRL);
	return 0;
}

static int msc313e_sar_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct msc313e_sar *sar = gpiochip_get_data(chip);
	u16 val = ioread16(sar->base + REG_GPIO_CTRL);
	val &= ~(BIT(offset) << 8);
	iowrite16(val, sar->base + REG_GPIO_CTRL);
	return 0;
}

static int msc313e_sar_probe_gpio(struct platform_device *pdev, struct msc313e_sar *sar)
{
	struct gpio_chip* gpiochip;
	int ret;
	gpiochip = devm_kzalloc(&pdev->dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpiochip->label            = DRIVER_NAME;
	gpiochip->parent		   = &pdev->dev;
	gpiochip->request          = msc313e_sar_gpio_request;
	gpiochip->free             = msc313e_sar_gpio_free;
	gpiochip->direction_input  = msc313e_sar_gpio_direction_input;
	gpiochip->get              = msc313e_sar_gpio_get;
	gpiochip->direction_output = msc313e_sar_gpio_direction_output;
	gpiochip->set              = msc313e_sar_gpio_set;
	gpiochip->base             = -1;
	gpiochip->ngpio            = 4;

	ret = gpiochip_add_data(gpiochip, sar);
	if (ret < 0) {
		dev_err(&pdev->dev,"failed to register gpio chip\n");
		goto out;
	}

	out:
	return ret;
}

static void msc313e_sar_continoussamplinghack(struct msc313e_sar *sar){
	iowrite16(0xa20, sar->base + REG_CTRL);
	iowrite16(0x5, sar->base + REG_SAMPLE_PERIOD);
	iowrite16(0x4a20, sar->base + REG_CTRL);
}

static int msc313e_sar_probe(struct platform_device *pdev)
{
	int ret = 0;
	const struct of_device_id *id;
	struct iio_dev *indio_dev;
	struct msc313e_sar *sar;
	struct resource *mem;
	int irq;

	printk("sar probe\n");

	id = of_match_node(msc313e_sar_dt_ids, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*sar));
	if(!indio_dev)
		return -ENOMEM;

	sar = iio_priv(indio_dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sar->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(sar->base))
		return PTR_ERR(sar->base);

	sar->clk = devm_clk_get(&pdev->dev, "sar_clk");
	if (IS_ERR(sar->clk)) {
		dev_err(&pdev->dev, "failed to get clk\n");
		return PTR_ERR(sar->clk);
	}

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!irq)
		return -EINVAL;

	ret = devm_request_irq(&pdev->dev, irq, msc313e_sar_irq, IRQF_SHARED,
			dev_name(&pdev->dev), indio_dev);
	if (ret)
		return ret;

	indio_dev->name = platform_get_device_id(pdev)->name;
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &msc313e_sar_iio_info;
	indio_dev->num_channels = ARRAY_SIZE(msc313e_sar_channels);
	indio_dev->channels = msc313e_sar_channels;

	platform_set_drvdata(pdev, indio_dev);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register iio device\n");
		goto out;
	}

	clk_prepare_enable(sar->clk);

	ret = msc313e_sar_probe_gpio(pdev, sar);

	msc313e_sar_continoussamplinghack(sar);

out:
	return ret;
}

static int msc313e_sar_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver msc313e_sar_driver = {
	.probe = msc313e_sar_probe,
	.remove = msc313e_sar_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = msc313e_sar_dt_ids,
	},
};

module_platform_driver(msc313e_sar_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mstar MSC313e SAR driver");
MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
