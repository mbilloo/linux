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
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "core.h"
#include "devicetree.h"
#include "pinconf.h"
#include "pinmux.h"

#define DRIVER_NAME			"pinctrl-mstar"

/*
 * There seems to be one main pinmux block in the chip at 0x1f203C00.
 * It looks like it controls the pins on a group basis and not by
 * selecting a function per pin.
 *
 * A pin becomes GPIO if all of the different functions that can be applied to the
 * pin are disabled.
 *
 * The SAR controls it's own pinmuxing.
 *
 * 0xc - UART
 *     9 8     | 7 6 |       5 4   | 3 2 |  1 0
 *    UART1    |  0  |     UART0   |  0  | FUART
 * 0x0         |	 | 0x0         |     | 0x0 - disabled?
 * 0x1         |	 | 0x1         |     | 0x1 - fuart
 * 0x2 - fuart |     | 0x2 - fuart |     | 0x2 - ??
 * 0x3         |     | 0x3         |     | 0x3 - ??
 *
 * 0x18 - SR
 * 5 4 | 3 | 2 1 0
 *  ?  | 0 |  SR
 *
 * 0x1c - PWM (maybe valid for the MSC313E only)
 *
 * 15 - 14      | 13 - 12       | 11 - 10       | 9 - 8
 * PWM7         | PWM6          | PWM5          | PWM4
 * 0x0          | 0x0           | 0x0           | 0x0
 * 0x1          | 0x1           | 0x1           | 0x1
 * 0x2 - spi_do | 0x2 - spi0_di | 0x2 - spi0_ck | 0x2 - spi0_cz
 * 0x3          | 0x3           | 0x3           | 0x3
 *
 * 7 6             | 5 4             | 3 2            | 1 0
 * PWM3            | PWM2            | PWM1           | PWM0
 * 0x0             | 0x0             | 0x0            | 0x0 - disabled ?
 * 0x1             | 0x1             | 0x1            | 0x1 - ??
 * 0x2 - fuart_rts | 0x2 - fuart_cts | 0x2            | 0x2 - ??
 * 0x3             | 0x3             | 0x3 - fuart_tx | 0x3 - fuart_rx
 *
 * 0x20 - SD/SDIO/NAND
 *    8      |
 * SDIO      |
 * 0x0       |
 * 0x1 - sd  |
 *
 * 0x24 - I2C
 * 15 - 12 | 7 | 5 4             | 3 2 | 1 0
 *         |   | I2C1            |  ?  | I2C0
 *         |   | 0x0 - disabled? |     | 0x0 - disabled?
 *             | 0x1 - i2c1      |     |
 * 0x30 - SPI
 * 6 | 5 4  | 3 2 | 1 0
 * ? | SPI1 |  ?  | SPI0
 *                | 0x0 - disabled ?
 *                | 0x1 - ??
 *                | 0x2 - ??
 *                | 0x3 - fuart
 *
 * 0x3c - JTAG, ETHERNET (TTL, CCIR)
 * 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 |        2        | 1 0
 * ?  | 0 | ? | 0 | ? | 0 | ? | 0 |    ETHERNET     | JTAG
 *                                  0x0 - disabled? | 0x0 - disabled ?
 *                                                  | 0x1 - fuart
 *                                                  | 0x2 - spi0
 *                                                  | 0x3 - ??
 *
 * - Toggling bits doesn't stop ethernet working :/
 *
 * 0x48 TEST_IN, TEST_OUT
 *
 * 0xC0 - nand drive
 * 0xC4 - nand pull up
 *
 * 0xC8 - SD/SDIO pull up/down, drive strength? rst : 0x0, can write 0x1f3f
 *         12 - 8       |           5 - 0
 *        pull en?      |          drv: 0?
 *  d3, d2, d1, d0, cmd | clk, d3, d2, d1, d0, cmd
 *
 * These might be internal dram pins
 * 0xe0 - ipl sets to 0xffff
 * 0xe4 - ipl sets to 0x3
 * 0xe8 - ipl sets to 0xffff
 * 0xec - ipl sets to 0x3
 * 0xf0 - ipl sets to 0
 * 0xf4 - ipl sets to 0
 *
 * 0x142 - sdk has this all over the place saying that it resets all pads to inputs
 */


struct mstar_pinctrl {
	struct device *dev;
	struct pinctrl_desc desc;
	struct pinctrl_dev *pctl;
	void __iomem *mux;
	struct regmap* regmap;
};

struct msc313e_pinctrl_function {
	const char* name;
	int reg;
	u16 mask;
	const char** groups;
	const u16* values;
	int numgroups;
};

struct msc313_pinctrl_group {
	const char* name;
	const int* pins;
	int numpins;
};

#define PIN_PM_SD_SDZ	15
#define PIN_PM_IRIN		16
#define PIN_PM_UART_RX	18
#define PIN_PM_UART_TX	19
#define PIN_PM_GPIO4	21
#define PIN_PM_SPI_CZ	22
#define PIN_PM_SPI_DI	23
#define PIN_PM_SPI_WPZ	24
#define PIN_PM_SPI_DO	25
#define PIN_PM_SPI_CK	26
#define PIN_ETH_RN		31
#define PIN_ETH_RP		32
#define PIN_ETH_TN		33
#define PIN_ETH_TP		34
#define PIN_FUART_RX	36
#define PIN_FUART_TX	37
#define PIN_FUART_CTS	38
#define PIN_FUART_RTS	39
#define PIN_I2C1_SCL	41
#define PIN_I2C1_SDA	42
#define PIN_SR_IO2		44
#define PIN_SR_IO3		45
#define PIN_SR_IO4		46
#define PIN_SR_IO5		47
#define PIN_SR_IO6		48
#define PIN_SR_IO7		49
#define PIN_SR_IO8		50
#define PIN_SR_IO9		51
#define PIN_SR_IO10		52
#define PIN_SR_IO11		53
#define PIN_SR_IO12		54
#define PIN_SR_IO13		55
#define PIN_SR_IO14		56
#define PIN_SR_IO15		57
#define PIN_SR_IO16		58
#define PIN_SR_IO17		59
#define PIN_SPI0_CZ		63
#define PIN_SPI0_CK		64
#define PIN_SPI0_DI		65
#define PIN_SPI0_DO		66
#define PIN_SD_CLK		68
#define PIN_SD_CMD		69
#define PIN_SD_D0		70
#define PIN_SD_D1		71
#define PIN_SD_D2		72
#define PIN_SD_D3		73
#define PIN_USB_DM		75
#define PIN_USB_DP		76

struct msc313_pinctrl_pin {
	int pin;
	int pullupreg;
	int pullupenbit;
	int drivereg;
	int drivebit;
};

static const struct msc313_pinctrl_pin pins[] = {
		{
			.pin = PIN_SD_CMD,
			.pullupreg = 0xc8,
			.pullupenbit = 8,
			.drivereg = 0xc8,
			.drivebit = 0
		},
		{
			.pin = PIN_SD_D0,
			.pullupreg = 0xc8,
			.pullupenbit = 9,
			.drivereg = 0xc8,
			.drivebit = 1
		},
		{
			.pin = PIN_SD_D1,
			.pullupreg = 0xc8,
			.pullupenbit = 10,
			.drivereg = 0xc8,
			.drivebit = 2
		},
		{
			.pin = PIN_SD_D2,
			.pullupreg = 0xc8,
			.pullupenbit = 11,
			.drivereg = 0xc8,
			.drivebit = 3
		},
		{
			.pin = PIN_SD_D3,
			.pullupreg = 0xc8,
			.pullupenbit = 12,
			.drivereg = 0xc8,
			.drivebit = 4
		},
		{
			.pin = PIN_SD_CLK,
			.pullupreg = -1,
			.pullupenbit = -1,
			.drivereg = 0xc8,
			.drivebit = 5
		}
};

static struct pinctrl_pin_desc msc313e_pins[] = {
	PINCTRL_PIN(PIN_PM_SD_SDZ,	"pm_sd_sdz"),
	PINCTRL_PIN(PIN_PM_IRIN,	"pm_irin"),
	PINCTRL_PIN(PIN_PM_UART_RX,	"pm_uart_rx"),
	PINCTRL_PIN(PIN_PM_UART_TX,	"pm_uart_tx"),
	PINCTRL_PIN(PIN_PM_GPIO4,	"pm_gpio4"),
	PINCTRL_PIN(PIN_PM_SPI_CZ,	"pm_spi_cz"),
	PINCTRL_PIN(PIN_PM_SPI_DI,	"pm_spi_di"),
	PINCTRL_PIN(PIN_PM_SPI_WPZ,	"pm_spi_wpz"),
	PINCTRL_PIN(PIN_PM_SPI_DO,	"pm_spi_do"),
	PINCTRL_PIN(PIN_PM_SPI_CK,	"pm_spi_ck"),
	PINCTRL_PIN(PIN_ETH_RN,		"eth_rn"),
	PINCTRL_PIN(PIN_ETH_RP,		"eth_rp"),
	PINCTRL_PIN(PIN_ETH_TN,		"eth_tn"),
	PINCTRL_PIN(PIN_ETH_TP,		"eth_tp"),
	PINCTRL_PIN(PIN_FUART_RX,	"fuart_rx"),
	PINCTRL_PIN(PIN_FUART_TX,	"fuart_tx"),
	PINCTRL_PIN(PIN_FUART_CTS,	"fuart_cts"),
	PINCTRL_PIN(PIN_FUART_RTS,	"fuart_rts"),
	PINCTRL_PIN(PIN_I2C1_SCL,	"i2c1_scl"),
	PINCTRL_PIN(PIN_I2C1_SDA,	"i2c1_sda"),
	PINCTRL_PIN(PIN_SR_IO2,		"sr_io2"),
	PINCTRL_PIN(PIN_SR_IO3,		"sr_io3"),
	PINCTRL_PIN(PIN_SR_IO4,		"sr_io4"),
	PINCTRL_PIN(PIN_SR_IO5,		"sr_io5"),
	PINCTRL_PIN(PIN_SR_IO6,		"sr_io6"),
	PINCTRL_PIN(PIN_SR_IO7,		"sr_io7"),
	PINCTRL_PIN(PIN_SR_IO8,		"sr_io8"),
	PINCTRL_PIN(PIN_SR_IO9,		"sr_io9"),
	PINCTRL_PIN(PIN_SR_IO10,	"sr_io10"),
	PINCTRL_PIN(PIN_SR_IO11,	"sr_io11"),
	PINCTRL_PIN(PIN_SR_IO12,	"sr_io12"),
	PINCTRL_PIN(PIN_SR_IO13,	"sr_io13"),
	PINCTRL_PIN(PIN_SR_IO14,	"sr_io14"),
	PINCTRL_PIN(PIN_SR_IO15,	"sr_io15"),
	PINCTRL_PIN(PIN_SR_IO16,	"sr_io16"),
	PINCTRL_PIN(PIN_SR_IO17,	"sr_io17"),
	PINCTRL_PIN(PIN_SPI0_CZ,	"spi0_cz"),
	PINCTRL_PIN(PIN_SPI0_CK,	"spi0_ck"),
	PINCTRL_PIN(PIN_SPI0_DI,	"spi0_di"),
	PINCTRL_PIN(PIN_SPI0_DO,	"spi0_do"),
	PINCTRL_PIN(PIN_SD_CLK,		"sd_clk"),
	PINCTRL_PIN(PIN_SD_CMD,		"sd_cmd"),
	PINCTRL_PIN(PIN_SD_D0,		"sd_d0"),
	PINCTRL_PIN(PIN_SD_D1,		"sd_d1"),
	PINCTRL_PIN(PIN_SD_D2,		"sd_d2"),
	PINCTRL_PIN(PIN_SD_D3,		"sd_d3"),
	PINCTRL_PIN(PIN_USB_DM,		"usb_dm"),
	PINCTRL_PIN(PIN_USB_DP,		"usb_dp"),
};

static int pm_uart_pins[] = { PIN_PM_UART_RX, PIN_PM_UART_TX };
static int pm_spi_pins[] = { PIN_PM_SPI_CZ, PIN_PM_SPI_DI,
		PIN_PM_SPI_WPZ, PIN_PM_SPI_DO, PIN_PM_SPI_CK };
static int eth_pins[] = { PIN_ETH_RN, PIN_ETH_RP, PIN_ETH_TN, PIN_ETH_TP };
static int fuart_pins[] = { PIN_FUART_RX, PIN_FUART_TX,PIN_FUART_CTS, PIN_FUART_RTS };
static int fuart_rx_pins[] = { PIN_FUART_RX };
static int fuart_tx_pins[] = { PIN_FUART_TX };
static int fuart_cts_pins[] = { PIN_FUART_CTS };
static int fuart_rts_pins[] = { PIN_FUART_RTS };
static int fuart_rx_tx_rts_pins[] = { PIN_FUART_RX, PIN_FUART_TX, PIN_FUART_RTS };
static int fuart_cts_rts_pins[] = { PIN_FUART_CTS, PIN_FUART_RTS };
static int i2c1_pins[] = { PIN_I2C1_SCL, PIN_I2C1_SDA };
static int spi0_pins[] = { PIN_SPI0_CZ, PIN_SPI0_CK, PIN_SPI0_DI, PIN_SPI0_DO };
static int spi0_cz_pins[] = { PIN_SPI0_CZ };
static int spi0_ck_pins[] = { PIN_SPI0_CK };
static int spi0_di_pins[] = { PIN_SPI0_DI };
static int spi0_do_pins[] = { PIN_SPI0_DO };
static int sd_d0_d1_d2_d3_pins[] = { PIN_SD_D0,PIN_SD_D1,PIN_SD_D2,PIN_SD_D3 };
static int sd_pins[] = { PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3 };

static int mstar_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np,
								map, num_maps,
								PIN_MAP_TYPE_INVALID);
}

static void mstar_dt_free_map(struct pinctrl_dev *pctldev,
			     struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops mstar_pinctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= mstar_dt_node_to_map,
	.dt_free_map		= mstar_dt_free_map,
};

static int mstar_set_mux(struct pinctrl_dev *pctldev, unsigned int func,
			   unsigned int group)
{
	struct mstar_pinctrl *pinctrl = pctldev->driver_data;
	const char *grpname = pinctrl_generic_get_group_name(pctldev, group);
	struct function_desc *funcdesc = pinmux_generic_get_function(pctldev, func);
	struct msc313e_pinctrl_function *function = funcdesc->data;
	int i, ret = 0;

	if(function != NULL){
		if(function->reg >= 0 && function->values != NULL){
			for(i = 0; i < function->numgroups; i++){
				if(strcmp(function->groups[i], grpname) == 0){
					dev_dbg(pinctrl->dev, "updating mux reg %x\n", (unsigned) function->reg);
					ret = regmap_update_bits(pinctrl->regmap, function->reg,
							function->mask, function->values[i]);
					if(ret)
						dev_dbg(pinctrl->dev, "failed to update register\n");
					break;
				}
			}
		}
		else {
			dev_dbg(pinctrl->dev, "reg or values not found\n");
		}
	}
	else {
		dev_info(pinctrl->dev, "missing function data\n");
	}

	return ret;
}

static const struct pinmux_ops mstar_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name   = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux             = mstar_set_mux,
	.strict              = true,
};

#define MSC313_PINCTRL_GROUP(n, p) { .name = n, .pins = p, .numpins = ARRAY_SIZE(p) }

static const struct msc313_pinctrl_group msc313_pinctrl_groups[] = {
		MSC313_PINCTRL_GROUP("pm_uart", pm_uart_pins),
		MSC313_PINCTRL_GROUP("pm_spi", pm_spi_pins),
		MSC313_PINCTRL_GROUP("eth", eth_pins),
		MSC313_PINCTRL_GROUP("fuart", fuart_pins),
		MSC313_PINCTRL_GROUP("fuart_rx", fuart_rx_pins),
		MSC313_PINCTRL_GROUP("fuart_tx", fuart_tx_pins),
		MSC313_PINCTRL_GROUP("fuart_cts", fuart_cts_pins),
		MSC313_PINCTRL_GROUP("fuart_rts", fuart_rts_pins),
		MSC313_PINCTRL_GROUP("fuart_rx_tx_rts", fuart_rx_tx_rts_pins),
		MSC313_PINCTRL_GROUP("fuart_cts_rts", fuart_cts_rts_pins),
		MSC313_PINCTRL_GROUP("i2c1", i2c1_pins),
		MSC313_PINCTRL_GROUP("spi0", spi0_pins),
		MSC313_PINCTRL_GROUP("spi0_cz", spi0_cz_pins),
		MSC313_PINCTRL_GROUP("spi0_ck", spi0_ck_pins),
		MSC313_PINCTRL_GROUP("spi0_di", spi0_di_pins),
		MSC313_PINCTRL_GROUP("spi0_do", spi0_do_pins),
		MSC313_PINCTRL_GROUP("sd_d0_d1_d2_d3", sd_d0_d1_d2_d3_pins),
		MSC313_PINCTRL_GROUP("sd", sd_pins),
};

static int mstar_pinctrl_parse_groups(struct mstar_pinctrl *pinctrl){
	int i, ret;
	for(i = 0; i < ARRAY_SIZE(msc313_pinctrl_groups); i++){
		const struct msc313_pinctrl_group *grp = &msc313_pinctrl_groups[i];
		ret = pinctrl_generic_add_group(pinctrl->pctl, grp->name,
				grp->pins, grp->numpins, NULL);
	}
	return ret;
}

static const char* pm_uart_groups[] = {"pm_uart"};
static const char* pm_spi_groups[] =  {"pm_spi"};
static const char* eth_groups[] =     {"eth"};
static const u16   eth_values[] =     {BIT(2)};
static const char* jtag_groups[] =    {"fuart"};
static const char* fuart_groups[] =   {"fuart", "fuart_rx_tx_rts"};
static const u16   fuart_values[] =   {BIT(0), BIT(0)};
static const char* uart0_groups[] =   {"fuart_rx_tx"};
static const char* uart1_groups[] =   {"fuart_cts_rts", "fuart_cts"};
static const u16   uart1_values[] =   {BIT(9), BIT(9)};
static const char* i2c1_groups[] =    {"i2c1"};
static const u16   i2c1_values[] =    {BIT(4)};
static const char* spi0_groups[] =    {"spi0", "fuart"};
static const u16   spi0_values[] =    {BIT(0), BIT(1) | BIT(0)};
static const char* spi1_groups[] =    {"sd_d0_d1_d2_d3"};
static const u16   spi1_values[] =    {BIT(5) | BIT(4)};
static const char* sdio_groups[] =    {"sd"};
static const u16   sdio_values[] =    {BIT(8)};
static const char* pwm0_groups[] =    {"fuart_rx"};
static const u16   pwm0_values[] =    {BIT(1) | BIT(0)};
static const char* pwm1_groups[] =    {"fuart_tx"};
static const u16   pwm1_values[] =    {BIT(3) | BIT(2)};
static const char* pwm2_groups[] =    {"fuart_cts"};
static const u16   pwm2_values[] =    {BIT(5)};
static const char* pwm3_groups[] =    {"fuart_rts"};
static const u16   pwm3_values[] =    {BIT(7)};
static const char* pwm4_groups[] =    {"spi0_cz"};
static const u16   pwm4_values[] =    {BIT(9)};
static const char* pwm5_groups[] =    {"spi0_ck"};
static const u16   pwm5_values[] =    {BIT(11)};
static const char* pwm6_groups[] =    {"spi0_di"};
static const u16   pwm6_values[] =    {BIT(13)};
static const char* pwm7_groups[] =    {"spi0_do"};
static const u16   pwm7_values[] =    {BIT(15)};

#define MSC313_PINCTRL_FUNCTION(n, r, m, g, v) \
		{ .name = n, .reg = r, .mask = m, .groups = g, .values = v, .numgroups = ARRAY_SIZE(g) }

static const struct msc313e_pinctrl_function msc313_pinctrl_functions[] = {
		MSC313_PINCTRL_FUNCTION("pm_uart", -1,   0,                pm_uart_groups, NULL),
		MSC313_PINCTRL_FUNCTION("pm_spi",  -1,   0,                pm_spi_groups,  NULL),
		MSC313_PINCTRL_FUNCTION("fuart",   0xc,  BIT(1) | BIT(0),  fuart_groups,   fuart_values),
		MSC313_PINCTRL_FUNCTION("uart0",   0xc,  BIT(5) | BIT(4),  uart0_groups,   NULL),
		MSC313_PINCTRL_FUNCTION("uart1",   0xc,  BIT(9) | BIT(8),  uart1_groups,   uart1_values),
		MSC313_PINCTRL_FUNCTION("pwm0",    0x1c, BIT(1) | BIT(0),  pwm0_groups,    pwm0_values),
		MSC313_PINCTRL_FUNCTION("pwm1",    0x1c, BIT(3) | BIT(2),  pwm1_groups,    pwm1_values),
		MSC313_PINCTRL_FUNCTION("pwm2",    0x1c, BIT(5) | BIT(4),  pwm2_groups,    pwm2_values),
		MSC313_PINCTRL_FUNCTION("pwm3",    0x1c, BIT(7) | BIT(6),  pwm3_groups,    pwm3_values),
		MSC313_PINCTRL_FUNCTION("pwm4",    0x1c, BIT(9) | BIT(8),  pwm4_groups,    pwm4_values),
		MSC313_PINCTRL_FUNCTION("pwm5",    0x1c, BIT(11)| BIT(10), pwm5_groups,    pwm5_values),
		MSC313_PINCTRL_FUNCTION("pwm6",    0x1c, BIT(13)| BIT(11), pwm6_groups,    pwm6_values),
		MSC313_PINCTRL_FUNCTION("pwm7",    0x1c, BIT(15)| BIT(14), pwm7_groups,    pwm7_values),
		MSC313_PINCTRL_FUNCTION("sdio",    0x20, BIT(8),           sdio_groups,    sdio_values),
		MSC313_PINCTRL_FUNCTION("i2c1",    0x24, BIT(5) | BIT(4),  i2c1_groups,    i2c1_values),
		MSC313_PINCTRL_FUNCTION("spi0",	   0x30, BIT(1) | BIT(0),  spi0_groups,    spi0_values),
		MSC313_PINCTRL_FUNCTION("spi1",	   0x30, BIT(5) | BIT(4),  spi1_groups,    spi1_values),
		MSC313_PINCTRL_FUNCTION("jtag",    0x3c, BIT(1) | BIT(0),  jtag_groups,    NULL),
		MSC313_PINCTRL_FUNCTION("eth",     0x3c, BIT(2),           eth_groups,     eth_values),
};

static int mstar_pinctrl_parse_functions(struct mstar_pinctrl *pinctrl){
	int i, ret;
	for(i = 0; i < ARRAY_SIZE(msc313_pinctrl_functions); i++){
		const struct msc313e_pinctrl_function *func =  &msc313_pinctrl_functions[i];

		// clear any existing value for the function
		if(func->reg >= 0){
			regmap_update_bits(pinctrl->regmap, func->reg,
					func->mask, 0);
		}

		ret = pinmux_generic_add_function(pinctrl->pctl, func->name,
											func->groups, func->numgroups,func);
		if(ret < 0){
			dev_err(pinctrl->dev, "failed to add function: %d", ret);
			goto out;
		}
	}
	out:
	return ret;
}

static const struct regmap_config msc313e_pinctrl_regmap_config = {
		.name = "msc313e-pinctrl",
		.reg_bits = 16,
		.val_bits = 16,
		.reg_stride = 4,
};

static int mstar_set_config(struct mstar_pinctrl *pinctrl, int pin, unsigned long config){
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);
	int i;
	unsigned int mask;
	dev_dbg(pinctrl->dev, "setting %d:%u on pin %d\n", (int)config,(unsigned)arg, pin);
	for(i = 0; i < ARRAY_SIZE(pins); i++){
		if(pins[i].pin == pin){
			switch(param){
			case PIN_CONFIG_BIAS_PULL_UP:
				if(pins[i].pullupreg != -1){
					dev_dbg(pinctrl->dev, "setting pull up %d on pin %d\n", (int) arg, pin);
					mask = 1 << pins[i].pullupenbit;
					regmap_update_bits(pinctrl->regmap, pins[i].pullupreg, mask, arg ? mask : 0);
				}
				else
					dev_dbg(pinctrl->dev, "pullup reg/bit isn't known for pin %d\n", pin);
			default:
				break;
			}
			return 0;
		}
	}
	return 0;
}

static int mstar_pinctrl_get_config(struct mstar_pinctrl *pinctrl, int pin, unsigned long *conf){
	int i;
	unsigned int mask;

	*conf = 0;
	for(i = 0; i < ARRAY_SIZE(pins); i++){
		if(pins[i].pin == pin){
			return 0;
		}
	}
	return -ENOTSUPP;
}

int mstar_pin_config_get(struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *config){
	return -ENOTSUPP;
}

int mstar_pin_config_set(struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *configs,
			       unsigned num_configs){
	return -ENOTSUPP;
}

int mstar_pin_config_group_get(struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *config){
	return -ENOTSUPP;
}

int mstar_pin_config_group_set(struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *configs,
				     unsigned num_configs){
	struct mstar_pinctrl *pinctrl = pctldev->driver_data;
	struct group_desc *group = pinctrl_generic_get_group(pctldev, selector);
	int i, j, ret;
	for(i = 0; i < group->num_pins; i++){
		for(j = 0; j < num_configs; j++){
			ret = mstar_set_config(pinctrl, group->pins[i], configs[j]);
			if(ret)
				return ret;
		}
	}
	return 0;
}

static const struct pinconf_ops mstar_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = mstar_pin_config_get,
	.pin_config_set = mstar_pin_config_set,
	.pin_config_group_get = mstar_pin_config_group_get,
	.pin_config_group_set = mstar_pin_config_group_set,
};


static int mstar_pinctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct mstar_pinctrl *pinctrl;
	struct resource *res;

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pinctrl);

	pinctrl->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pinctrl->mux = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pinctrl->mux))
		return PTR_ERR(pinctrl->mux);

	pinctrl->regmap = devm_regmap_init_mmio(pinctrl->dev, pinctrl->mux,
			&msc313e_pinctrl_regmap_config);
	if(IS_ERR(pinctrl->regmap)){
		dev_err(pinctrl->dev, "failed to register regmap");
		return PTR_ERR(pinctrl->regmap);
	}

	pinctrl->desc.name = DRIVER_NAME;
	pinctrl->desc.pctlops = &mstar_pinctrl_ops;
	pinctrl->desc.pmxops = &mstar_pinmux_ops;
	pinctrl->desc.confops = &mstar_pinconf_ops;
	pinctrl->desc.owner = THIS_MODULE;
	pinctrl->desc.pins = msc313e_pins;
	pinctrl->desc.npins = ARRAY_SIZE(msc313e_pins);

	ret = devm_pinctrl_register_and_init(pinctrl->dev, &pinctrl->desc,
					     pinctrl, &pinctrl->pctl);

	if (ret) {
		dev_err(pinctrl->dev, "failed to register pinctrl\n");
		return ret;
	}

	ret = mstar_pinctrl_parse_functions(pinctrl);
	ret = mstar_pinctrl_parse_groups(pinctrl);

	ret = pinctrl_enable(pinctrl->pctl);
	if (ret)
		dev_err(pinctrl->dev, "failed to enable pinctrl\n");

	return 0;
}

static const struct of_device_id mstar_pinctrl_of_match[] = {
	{
		.compatible	= "mstar,msc313e-pinctrl",
	},
	{ }
};

static struct platform_driver mstar_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mstar_pinctrl_of_match,
	},
	.probe = mstar_pinctrl_probe,
};

static int __init mstar_pinctrl_init(void)
{
	return platform_driver_register(&mstar_pinctrl_driver);
}
core_initcall(mstar_pinctrl_init);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("Pin controller driver for MStar MSC313E SoCs");
MODULE_LICENSE("GPL v2");
