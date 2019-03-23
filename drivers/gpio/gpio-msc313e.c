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
#include <linux/slab.h>

#define DRIVER_NAME			"gpio-msc313e"

/*
 * gpio data registers seem to be laid out like this in the gpio block
 *  5   |  4  | 3 | 2 | 1 | 0
 * ~OEN | OUT | 0 | 0 | 0 | IN
 */

#define MSTAR_GPIO_IN  BIT(0)
#define MSTAR_GPIO_OUT BIT(4)
#define MSTAR_GPIO_OEN BIT(5)

struct msc313e_gpio {
	void __iomem *base;
};

static u16 gpiooffsets[] = {
		0x50, /* FUART_RX	*/
		0x54, /* FUART_TX	*/
		0x58, /* FUART_CTS	*/
		0x5c, /* FUART_RTX	*/
		0x188, /* I2C1_SCL	*/
		0x18c, /* I2C1_SDA  */
		0x88, /* SR_IO2		*/
		0x8c, /* SR_IO3		*/
		0x90, /* SR_IO4		*/
		0x94, /* SR_IO5		*/
		0x98, /* SR_IO6		*/
		0x9c, /* SR_IO7		*/
		0xa0, /* SR_IO8		*/
		0xa4, /* SR_IO9		*/
		0xa8, /* SR_IO10	*/
		0xb0, /* SR_IO11	*/
		0xb4, /* SR_IO12	*/
		0xb8, /* SR_IO13	*/
		0xbc, /* SR_IO14	*/
		0xc0, /* SR_IO15	*/
		0xc4, /* SR_IO16	*/
		0xc8, /* SR_IO17	*/
		0x1c0, /* SPI0_CZ	*/
		0x1c4, /* SPI0_CK	*/
		0x1c8, /* SPI0_DI	*/
		0x1cc, /* SPI0_DO	*/
		0x140, /* SD_CLK	*/
		0x144, /* SD_CMD	*/
		0x148, /* SD_D0		*/
		0x14c, /* SD_D1		*/
		0x150, /* SD_D2		*/
		0x154, /* SD_D3		*/
};

static int mstar_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static void mstar_gpio_free(struct gpio_chip *chip, unsigned offset)
{
}

static void mstar_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct msc313e_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = ioread8(gpio->base + gpiooffsets[offset]);
	if(value)
		gpioreg |= MSTAR_GPIO_OUT;
	else
		gpioreg &= ~MSTAR_GPIO_OUT;
	iowrite8(gpioreg, gpio->base + gpiooffsets[offset]);
}

static int mstar_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_gpio *gpio = gpiochip_get_data(chip);
	return ioread8(gpio->base + gpiooffsets[offset]) & MSTAR_GPIO_IN;
}

static int mstar_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct msc313e_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = ioread8(gpio->base + gpiooffsets[offset]);
	gpioreg |= MSTAR_GPIO_OEN;
	iowrite8(gpioreg, gpio->base + gpiooffsets[offset]);
	return 0;
}

static int mstar_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct msc313e_gpio *gpio = gpiochip_get_data(chip);
	u8 gpioreg = ioread8(gpio->base + gpiooffsets[offset]);
	gpioreg &= ~MSTAR_GPIO_OEN;
	if(value)
		gpioreg |= MSTAR_GPIO_OUT;
	else
		gpioreg &= ~MSTAR_GPIO_OUT;
	iowrite8(gpioreg, gpio->base + gpiooffsets[offset]);
	return 0;
}

static int msc313e_gpio_probe(struct platform_device *pdev)
{
	int ret;
	struct msc313e_gpio *gpio;
	struct resource *res;
	struct gpio_chip* gpiochip;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	platform_set_drvdata(pdev, gpio);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gpio->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	gpiochip = devm_kzalloc(&pdev->dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpiochip->label            = DRIVER_NAME;
	gpiochip->parent		   = &pdev->dev;
	gpiochip->request          = mstar_gpio_request;
	gpiochip->free             = mstar_gpio_free;
	gpiochip->direction_input  = mstar_gpio_direction_input;
	gpiochip->get              = mstar_gpio_get;
	gpiochip->direction_output = mstar_gpio_direction_output;
	gpiochip->set              = mstar_gpio_set;
	gpiochip->base             = -1;
	gpiochip->ngpio            = ARRAY_SIZE(gpiooffsets);

	ret = gpiochip_add_data(gpiochip, gpio);
	return ret;
}

static const struct of_device_id msc313e_gpio_of_match[] = {
	{
		.compatible	= "mstar,msc313e-gpio",
	},
	{ }
};

static struct platform_driver msc313e_gpio_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = msc313e_gpio_of_match,
	},
	.probe = msc313e_gpio_probe,
};

static int __init msc313e_gpio_init(void)
{
	return platform_driver_register(&msc313e_gpio_driver);
}
core_initcall(msc313e_gpio_init);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("gpio controller driver for MStar MSC313E");
MODULE_LICENSE("GPL v2");
