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

static const struct pinctrl_pin_desc rza1_pins [] = {
		// port 0
		{ .number = 0,	.name = "P0_0",		.drv_data = NULL },
		{ .number = 1,	.name = "P0_1",		.drv_data = NULL },
		{ .number = 2,	.name = "P0_2",		.drv_data = NULL },
		{ .number = 3,	.name = "P0_3",		.drv_data = NULL },
		{ .number = 4,	.name = "P0_4",		.drv_data = NULL },
		{ .number = 5,	.name = "P0_5",		.drv_data = NULL },
		// port 1
		{ .number = 6,	.name = "P1_0",		.drv_data = NULL },
		{ .number = 7,	.name = "P1_1",		.drv_data = NULL },
		{ .number = 8,	.name = "P1_2",		.drv_data = NULL },
		{ .number = 9,	.name = "P1_3",		.drv_data = NULL },
		{ .number = 10,	.name = "P1_4",		.drv_data = NULL },
		{ .number = 11,	.name = "P1_5",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(1,6),	.name = "P1_6",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(1,7),	.name = "P1_7",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(1,8),	.name = "P1_8",		.drv_data = NULL },
		{ .number = 15,	.name = "P1_9",		.drv_data = NULL },
		{ .number = 16,	.name = "P1_10",	.drv_data = NULL },
		{ .number = 17,	.name = "P1_11",	.drv_data = NULL },
		{ .number = 18,	.name = "P1_12",	.drv_data = NULL },
		{ .number = 19,	.name = "P1_13",	.drv_data = NULL },
		{ .number = 20,	.name = "P1_14",	.drv_data = NULL },
		{ .number = 21,	.name = "P1_15",	.drv_data = NULL },
		// port 2
		{ .number = 22,	.name = "P2_0",		.drv_data = NULL },
		{ .number = 23,	.name = "P2_1",		.drv_data = NULL },
		{ .number = 24,	.name = "P2_2",		.drv_data = NULL },
		{ .number = 25,	.name = "P2_3",		.drv_data = NULL },
		{ .number = 26,	.name = "P2_4",		.drv_data = NULL },
		{ .number = 27,	.name = "P2_5",		.drv_data = NULL },
		{ .number = 28,	.name = "P2_6",		.drv_data = NULL },
		{ .number = 29,	.name = "P2_7",		.drv_data = NULL },
		{ .number = 30,	.name = "P2_8",		.drv_data = NULL },
		{ .number = 31,	.name = "P2_9",		.drv_data = NULL },
		{ .number = 32,	.name = "P2_10",	.drv_data = NULL },
		{ .number = 33,	.name = "P2_11",	.drv_data = NULL },
		{ .number = 34,	.name = "P2_12",	.drv_data = NULL },
		{ .number = 35,	.name = "P2_13",	.drv_data = NULL },
		{ .number = 36,	.name = "P2_14",	.drv_data = NULL },
		{ .number = 37,	.name = "P2_15",	.drv_data = NULL },
		// port 3
		{ .number = 38,	.name = "P3_0",		.drv_data = NULL },
		{ .number = 39,	.name = "P3_1",		.drv_data = NULL },
		{ .number = 40,	.name = "P3_2",		.drv_data = NULL },
		{ .number = 41,	.name = "P3_3",		.drv_data = NULL },
		{ .number = 42,	.name = "P3_4",		.drv_data = NULL },
		{ .number = 43,	.name = "P3_5",		.drv_data = NULL },
		{ .number = 44,	.name = "P3_6",		.drv_data = NULL },
		{ .number = 45,	.name = "P3_7",		.drv_data = NULL },
		{ .number = 46,	.name = "P3_8",		.drv_data = NULL },
		{ .number = 47,	.name = "P3_9",		.drv_data = NULL },
		{ .number = 48,	.name = "P3_10",	.drv_data = NULL },
		{ .number = 49,	.name = "P3_11",	.drv_data = NULL },
		{ .number = 50,	.name = "P3_12",	.drv_data = NULL },
		{ .number = 51,	.name = "P3_13",	.drv_data = NULL },
		{ .number = 52,	.name = "P3_14",	.drv_data = NULL },
		{ .number = 53,	.name = "P3_15",	.drv_data = NULL },
		// port 4
		{ .number = 54,	.name = "P4_0",		.drv_data = NULL },
		{ .number = 55,	.name = "P4_1",		.drv_data = NULL },
		{ .number = 56,	.name = "P4_2",		.drv_data = NULL },
		{ .number = 57,	.name = "P4_3",		.drv_data = NULL },
		{ .number = 58,	.name = "P4_4",		.drv_data = NULL },
		{ .number = 59,	.name = "P4_5",		.drv_data = NULL },
		{ .number = 60,	.name = "P4_6",		.drv_data = NULL },
		{ .number = 61,	.name = "P4_7",		.drv_data = NULL },
		{ .number = 62,	.name = "P4_8",		.drv_data = NULL },
		{ .number = 63,	.name = "P4_9",		.drv_data = NULL },
		{ .number = 64,	.name = "P4_10",	.drv_data = NULL },
		{ .number = 65,	.name = "P4_11",	.drv_data = NULL },
		{ .number = 66,	.name = "P4_12",	.drv_data = NULL },
		{ .number = 67,	.name = "P4_13",	.drv_data = NULL },
		{ .number = 68,	.name = "P4_14",	.drv_data = NULL },
		{ .number = 69,	.name = "P4_15",	.drv_data = NULL },
		// port 5
		{ .number = 70,	.name = "P5_0",		.drv_data = NULL },
		{ .number = 71,	.name = "P5_1",		.drv_data = NULL },
		{ .number = 72,	.name = "P5_2",		.drv_data = NULL },
		{ .number = 73,	.name = "P5_3",		.drv_data = NULL },
		{ .number = 74,	.name = "P5_4",		.drv_data = NULL },
		{ .number = 75,	.name = "P5_5",		.drv_data = NULL },
		{ .number = 76,	.name = "P5_6",		.drv_data = NULL },
		{ .number = 77,	.name = "P5_7",		.drv_data = NULL },
		{ .number = 78,	.name = "P5_8",		.drv_data = NULL },
		{ .number = 79,	.name = "P5_9",		.drv_data = NULL },
		{ .number = 80,	.name = "P5_10",	.drv_data = NULL },
		// port 6
		{ .number = 81,	.name = "P6_0",		.drv_data = NULL },
		{ .number = 82,	.name = "P6_1",		.drv_data = NULL },
		{ .number = 83,	.name = "P6_2",		.drv_data = NULL },
		{ .number = 84,	.name = "P6_3",		.drv_data = NULL },
		{ .number = 85,	.name = "P6_4",		.drv_data = NULL },
		{ .number = 86,	.name = "P6_5",		.drv_data = NULL },
		{ .number = 87,	.name = "P6_6",		.drv_data = NULL },
		{ .number =	88,	.name = "P6_7",		.drv_data = NULL },
		{ .number = 89,	.name = "P6_8",		.drv_data = NULL },
		{ .number = 90,	.name = "P6_9",		.drv_data = NULL },
		{ .number = 91,	.name = "P6_10",	.drv_data = NULL },
		{ .number = 92,	.name = "P6_11",	.drv_data = NULL },
		{ .number = 93,	.name = "P6_12",	.drv_data = NULL },
		{ .number = 94,	.name = "P6_13",	.drv_data = NULL },
		{ .number = 95,.name = "P6_14",	.drv_data = NULL },
		{ .number = 96,.name = "P6_15",	.drv_data = NULL },
		// port 7
		{ .number = 97,.name = "P7_0",		.drv_data = NULL },
		{ .number = 98,.name = "P7_1",		.drv_data = NULL },
		{ .number = 99,.name = "P7_2",		.drv_data = NULL },
		{ .number = 100,.name = "P7_3",		.drv_data = NULL },
		{ .number = 101,.name = "P7_4",		.drv_data = NULL },
		{ .number = 102,.name = "P7_5",		.drv_data = NULL },
		{ .number = 103,.name = "P7_6",		.drv_data = NULL },
		{ .number = 104,.name = "P7_7",		.drv_data = NULL },
		{ .number = 105,.name = "P7_8",		.drv_data = NULL },
		{ .number = 106,.name = "P7_9",		.drv_data = NULL },
		{ .number = 107,.name = "P7_10",	.drv_data = NULL },
		{ .number = 108,.name = "P7_11",	.drv_data = NULL },
		{ .number = 109,.name = "P7_12",	.drv_data = NULL },
		{ .number = 110,.name = "P7_13",	.drv_data = NULL },
		{ .number = 111,.name = "P7_14",	.drv_data = NULL },
		{ .number = 112,.name = "P7_15",	.drv_data = NULL },
		// port 8
		{ .number = RZA1PFC_PIN(8,0),		.name = "P8_0",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,1),		.name = "P8_1",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,2),		.name = "P8_2",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,3),		.name = "P8_3",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,4),		.name = "P8_4",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,5),		.name = "P8_5",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,6),		.name = "P8_6",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,7),		.name = "P8_7",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,8),		.name = "P8_8",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,9),		.name = "P8_9",		.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,10),		.name = "P8_10",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,11),		.name = "P8_11",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,12),		.name = "P8_12",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,13),		.name = "P8_13",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,14),		.name = "P8_14",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(8,15),		.name = "P8_15",	.drv_data = NULL },
		// port 9
		{ .number = 129,.name = "P9_0",		.drv_data = NULL },
		{ .number = 130,.name = "P9_1",		.drv_data = NULL },
		{ .number = 131,.name = "P9_2",		.drv_data = NULL },
		{ .number = 132,.name = "P9_3",		.drv_data = NULL },
		{ .number = 133,.name = "P9_4",		.drv_data = NULL },
		{ .number = 134,.name = "P9_5",		.drv_data = NULL },
		{ .number = 135,.name = "P9_6",		.drv_data = NULL },
		{ .number = 136,.name = "P9_7",		.drv_data = NULL },
		// port 10
		{ .number = RZA1PFC_PIN(10,0),	.name = "P10_0",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,1),	.name = "P10_1",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,2),	.name = "P10_2",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,3),	.name = "P10_3",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,4),	.name = "P10_4",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,5),	.name = "P10_5",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,6),	.name = "P10_6",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,7),	.name = "P10_7",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,8),	.name = "P10_8",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,9),	.name = "P10_9",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,10),	.name = "P10_10",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,11),	.name = "P10_11",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,12),	.name = "P10_12",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,13),	.name = "P10_13",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,14),	.name = "P10_14",	.drv_data = NULL },
		{ .number = RZA1PFC_PIN(10,15),	.name = "P10_15",	.drv_data = NULL },
		// port 11
		{ .number = 153,.name = "P11_0",	.drv_data = NULL },
		{ .number = 154,.name = "P11_1",	.drv_data = NULL },
		{ .number = 155,.name = "P11_2",	.drv_data = NULL },
		{ .number = 156,.name = "P11_3",	.drv_data = NULL },
		{ .number = 157,.name = "P11_4",	.drv_data = NULL },
		{ .number = 158,.name = "P11_5",	.drv_data = NULL },
		{ .number = 159,.name = "P11_6",	.drv_data = NULL },
		{ .number = 160,.name = "P11_7",	.drv_data = NULL },
		{ .number = 161,.name = "P11_8",	.drv_data = NULL },
		{ .number = 162,.name = "P11_9",	.drv_data = NULL },
		{ .number = 163,.name = "P11_10",	.drv_data = NULL },
		{ .number = 164,.name = "P11_11",	.drv_data = NULL },
		{ .number = 165,.name = "P11_12",	.drv_data = NULL },
		{ .number = 166,.name = "P11_13",	.drv_data = NULL },
		{ .number = 167,.name = "P11_14",	.drv_data = NULL },
		{ .number = 168,.name = "P11_15",	.drv_data = NULL },
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
	if ((port == 0) && (dir != DIR_IN))	/* p0 is input only */
		return -1;

	if (dir == DIR_IN) {
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
	set_direction(&gpio->pfc->base, gpio->port, pin, DIR_IN);
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
	set_direction(gpio->pfc->base, gpio->port, pin, DIR_OUT);
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
	int port = (pin >> RZA1PFC_PORT_SHIFT) & 0xff, bit = pin & 0xff;

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
			if (dir == DIR_IN) {
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
	if ((dir == DIIO_PBDC_EN) || (dir == SWIO_OUT_PBDCEN))
		bit_modify(base, PBDC(port), bit, true);	/* Bidirection enable */

	/* Port Mode */
	if (mode == RZA1PFC_MODE_GPIO) {
		if (dir == DIR_IN) {
			bit_modify(base, PIBC(port), bit, true); /*Inputbuffer allow*/
		} else if (dir == PORT_OUT_LOW) {
			bit_modify(base, PORT(port), bit, false); /*Output low level*/
			bit_modify(base, PM(port), bit, false);  /* Output mode */
		} else if (dir == PORT_OUT_HIGH) {
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
		if ((dir == DIIO_PBDC_DIS) || (dir == DIIO_PBDC_EN))
			bit_modify(base, PIPC(port), bit, true); /* direct I/O cont */
		/* PMC Setting */
		bit_modify(base, PMC(port), bit, true);	/* Alternative Mode */

		/* PM Setting : Output mode (output enabled)*/
		if ((dir == SWIO_OUT_PBDCDIS) || (dir == SWIO_OUT_PBDCEN))
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
	struct resource *base_res;
	void __iomem *base;
	int i, j, retval, length;
	const __be32 *p;
	u32 val;

	int port, npins;

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

	rza1pinctrl->base = base;
	mutex_init(&rza1pinctrl->mutex);

	rza1pinctrl->pctl_desc.name = dev_name(&pdev->dev);
	rza1pinctrl->pctl_desc.owner = THIS_MODULE;
	rza1pinctrl->pctl_desc.pins = rza1_pins;
	rza1pinctrl->pctl_desc.npins = ARRAY_SIZE(rza1_pins);
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

	rza1pinctrl->pctl_dev = devm_pinctrl_register(&pdev->dev, &rza1pinctrl->pctl_desc,
					       rza1pinctrl);
	if (IS_ERR(rza1pinctrl->pctl_dev)) {
		dev_err(&pdev->dev, "Failed pinctrl registration\n");
		//return PTR_ERR(rza1pinctrl->pctl_dev);
	}


	// find and register gpios
	for_each_child_of_node(np, child) {
			prop = of_find_property(child, "gpio-controller", &length);
			if(prop){
				if(of_property_read_u32(child, "renesas,port", &port))
					continue;
				if(of_property_read_u32(child, "ngpios", &npins))
					continue;
				rza1pfc_gpio_registerport(pdev, child, rza1pinctrl, port, npins);
			}
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
