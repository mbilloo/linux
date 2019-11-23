// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
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
 * 0x0         |     | 0x0         |     | 0x0 - disabled?
 * 0x1         |     | 0x1         |     | 0x1 - fuart
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
 *             | 0x1 - i2c1      |     | 0x1 - i2c0
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
 *
 *
 * Mercury 5 notes:
 *
 * Offsets from dashcam app
 *
 *
 * 0x3c - I2S/DMIC/TTL/CCIR
 * 0x4c - EMMC
 * 0x50 - SPI2/SD30/RGB_8B/RGB_24B/
 * 0x54 - SR1_MIPI
 * 0x58 - TX_MIPI
 * 0x5c - I2C1_DUAL
 *
 */

/* standard pin names that are used across mstar/sigmastars parts */

#define PINNAME_PM_UART_RX	"pm_uart_rx"
#define PINNAME_PM_UART_TX	"pm_uart_tx"
#define PINNAME_PM_SD_CDZ	"pm_sd_cdz"
#define PINNAME_PM_IRIN		"pm_irin"
#define PINNAME_PM_GPIO0	"pm_gpio0"
#define PINNAME_PM_GPIO2	"pm_gpio2"
#define PINNAME_PM_GPIO4	"pm_gpio4"
#define PINNAME_PM_GPIO5	"pm_gpio5"
#define PINNAME_PM_GPIO6	"pm_gpio6"
#define PINNAME_PM_GPIO8	"pm_gpio8"
#define PINNAME_PM_SPI_CZ	"pm_spi_cz"
#define PINNAME_PM_SPI_DI	"pm_spi_di"
#define PINNAME_PM_SPI_WPZ	"pm_spi_wpz"
#define PINNAME_PM_SPI_DO	"pm_spi_do"
#define PINNAME_PM_SPI_CK	"pm_spi_ck"
#define PINNAME_PM_SPI_HOLD	"pm_spi_hold"
#define PINNAME_FUART_TX	"fuart_tx"
#define PINNAME_FUART_RX	"fuart_rx"
#define	PINNAME_FUART_CTS	"fuart_cts"
#define PINNAME_FUART_RTS	"fuart_rts"
#define PINNAME_SPI0_CZ		"spi0_cz"
#define PINNAME_SPI0_CZ1	"spi0_cz1"
#define PINNAME_SPI0_CK		"spi0_ck"
#define PINNAME_SPI0_DI		"spi0_di"
#define PINNAME_SPI0_DO		"spi0_do"
#define PINNAME_SD_CLK		"sd_clk"
#define PINNAME_SD_CMD		"sd_cmd"
#define PINNAME_SD_D0		"sd_d0"
#define PINNAME_SD_D1		"sd_d1"
#define PINNAME_SD_D2		"sd_d2"
#define PINNAME_SD_D3		"sd_d3"
#define PINNAME_USB_DM		"usb_dm"
#define PINNAME_USB_DP		"usb_dp"
#define PINNAME_USB_DM1		"usb_dm1"
#define PINNAME_USB_DP1		"usb_dp1"
#define PINNAME_USB_CID		"usb_cid"
#define PINNAME_I2C0_SCL	"i2c0_scl"
#define PINNAME_I2C0_SDA	"i2c0_sda"
#define PINNAME_I2C1_SCL	"i2c1_scl"
#define PINNAME_I2C1_SDA	"i2c1_sda"
#define PINNAME_ETH_RN		"eth_rn"
#define PINNAME_ETH_RP		"eth_rp"
#define PINNAME_ETH_TN		"eth_tn"
#define PINNAME_ETH_TP		"eth_tp"
#define PINNAME_LCD_DE		"lcd_de"
#define PINNAME_LCD_PCLK	"lcd_pclk"
#define PINNAME_LCD_VSYNC	"lcd_vsync"
#define PINNAME_LCD_HSYNC	"lcd_hsync"
#define PINNAME_LCD_0		"lcd_0"
#define PINNAME_LCD_1		"lcd_1"
#define PINNAME_LCD_2		"lcd_2"
#define PINNAME_LCD_3		"lcd_3"
#define PINNAME_LCD_4		"lcd_4"
#define PINNAME_LCD_5		"lcd_5"
#define PINNAME_LCD_6		"lcd_6"
#define PINNAME_LCD_7		"lcd_7"
#define PINNAME_LCD_8		"lcd_8"
#define PINNAME_LCD_9		"lcd_9"
#define PINNAME_LCD_10		"lcd_10"
#define PINNAME_LCD_11		"lcd_11"
#define PINNAME_LCD_12		"lcd_12"
#define PINNAME_LCD_13		"lcd_13"
#define PINNAME_LCD_14		"lcd_14"
#define PINNAME_LCD_15		"lcd_15"

/*
 * for later parts with more sensor interfaces
 * the pin naming seems to have changed
 */

#define PINNAME_SR0_D0		"sr0_d0"

/* common pin group names */

#define GROUPNAME_PM_UART	"pm_uart"
#define GROUPNAME_PM_SPI	"pm_spi"
#define GROUPNAME_SD		"sd"
#define GROUPNAME_USB		"usb"
#define GROUPNAME_USB1		"usb1"
#define GROUPNAME_I2C0		"i2c0"
#define GROUPNAME_I2C1		"i2c1"
#define GROUPNAME_FUART		"fuart"
#define GROUPNAME_ETH		"eth"
#define GROUPNAME_SPI0		"spi0"

/* common group function names */

#define FUNCTIONNAME_PM_UART	GROUPNAME_PM_UART
#define FUNCTIONNAME_PM_SPI	GROUPNAME_PM_SPI
#define FUNCTIONNAME_USB	GROUPNAME_USB
#define FUNCTIONNAME_USB1	GROUPNAME_USB1
#define FUNCTIONNAME_FUART	GROUPNAME_FUART
#define FUNCTIONNAME_ETH	GROUPNAME_ETH
#define FUNCTIONNAME_SPI0	GROUPNAME_SPI0

#define COMMON_PIN(_model, _pinname) PINCTRL_PIN(PIN_##_model##_##_pinname, PINNAME_##_pinname)

#define MSTAR_PINCTRL_FUNCTION(n, r, m, g, v) \
		{ .name = n, .reg = r, .mask = m, .groups = g, .values = v, .numgroups = ARRAY_SIZE(g) }
#define MSTAR_PINCTRL_GROUP(n, p) { .name = n, .pins = p, .numpins = ARRAY_SIZE(p) }

struct mstar_pinctrl_function {
	const char* name;
	int reg;
	u16 mask;
	const char** groups;
	const u16* values;
	int numgroups;
};

struct mstar_pinctrl_group {
	const char* name;
	const int* pins;
	const int numpins;
};

struct mstar_configurable_pin {
	const int pin;
	const int pullupreg;
	const int pullupenbit;
	const int drivereg;
	const int drivebit;
	const unsigned *drivecurrents;
	const int ndrivecurrents;
};

struct mstar_pinctrl_info {
	const struct pinctrl_pin_desc *pins;
	const int npins;
	const struct mstar_pinctrl_group *groups;
	const int ngroups;
	const struct mstar_pinctrl_function *functions;
	const int nfunctions;
	const struct mstar_configurable_pin *confpin;
	const int nconfpins;
};

struct mstar_pinctrl {
	struct device *dev;
	struct pinctrl_desc desc;
	struct pinctrl_dev *pctl;
	void __iomem *mux;
	struct regmap *regmap;
	const struct mstar_pinctrl_info *info;
};

/* common groups and register values */
static const char* i2c0_groups[] =    { GROUPNAME_I2C0 };
static const u16   i2c0_values[] =    { BIT(0) };
static const char* pm_uart_groups[] = { GROUPNAME_PM_UART };
static const char* pm_spi_groups[] =  { GROUPNAME_PM_SPI };
static const char* usb_groups[] =     { GROUPNAME_USB };
static const char* usb1_groups[] =    { GROUPNAME_USB1 };

#define COMMON_FIXED_FUNCTION(_NAME,_name) MSTAR_PINCTRL_FUNCTION(FUNCTIONNAME_##_NAME, -1, 0, _name##_groups, NULL)
#define COMMON_FUNCTION(_NAME,_name) MSTAR_PINCTRL_FUNCTION(FUNCTIONNAME_##_NAME, -1, 0, _name##_groups, _name##_values)

#define COMMON_FUNCTIONS COMMON_FIXED_FUNCTION(PM_UART, pm_uart), \
				COMMON_FIXED_FUNCTION(PM_SPI, pm_spi), \
				COMMON_FIXED_FUNCTION(USB, usb), \
				COMMON_FUNCTION(FUART, fuart)

/* MSC313/MSC313E */
/* Chip pin numbers */
#define PIN_MSC313_PM_SD_CDZ	15
#define PIN_MSC313_PM_IRIN	16
#define PIN_MSC313_PM_UART_RX	18
#define PIN_MSC313_PM_UART_TX	19
#define PIN_MSC313_PM_GPIO4	21
#define PIN_MSC313_PM_SPI_CZ	22
#define PIN_MSC313_PM_SPI_DI	23
#define PIN_MSC313_PM_SPI_WPZ	24
#define PIN_MSC313_PM_SPI_DO	25
#define PIN_MSC313_PM_SPI_CK	26
#define PIN_MSC313_ETH_RN	31
#define PIN_MSC313_ETH_RP	32
#define PIN_MSC313_ETH_TN	33
#define PIN_MSC313_ETH_TP	34
#define PIN_MSC313_FUART_RX	36
#define PIN_MSC313_FUART_TX	37
#define PIN_MSC313_FUART_CTS	38
#define PIN_MSC313_FUART_RTS	39
#define PIN_MSC313_I2C1_SCL	41
#define PIN_MSC313_I2C1_SDA	42
#define PIN_MSC313_SR_IO2	44
#define PIN_MSC313_SR_IO3	45
#define PIN_MSC313_SR_IO4	46
#define PIN_MSC313_SR_IO5	47
#define PIN_MSC313_SR_IO6	48
#define PIN_MSC313_SR_IO7	49
#define PIN_MSC313_SR_IO8	50
#define PIN_MSC313_SR_IO9	51
#define PIN_MSC313_SR_IO10	52
#define PIN_MSC313_SR_IO11	53
#define PIN_MSC313_SR_IO12	54
#define PIN_MSC313_SR_IO13	55
#define PIN_MSC313_SR_IO14	56
#define PIN_MSC313_SR_IO15	57
#define PIN_MSC313_SR_IO16	58
#define PIN_MSC313_SR_IO17	59
#define PIN_MSC313_SPI0_CZ	63
#define PIN_MSC313_SPI0_CK	64
#define PIN_MSC313_SPI0_DI	65
#define PIN_MSC313_SPI0_DO	66
#define PIN_MSC313_SD_CLK	68
#define PIN_MSC313_SD_CMD	69
#define PIN_MSC313_SD_D0	70
#define PIN_MSC313_SD_D1	71
#define PIN_MSC313_SD_D2	72
#define PIN_MSC313_SD_D3	73
#define PIN_MSC313_USB_DM	75
#define PIN_MSC313_USB_DP	76

#define MSC313_COMMON_PIN(_pinname) COMMON_PIN(MSC313, _pinname)

/* pinctrl pins */
static struct pinctrl_pin_desc msc313_pins[] = {
	MSC313_COMMON_PIN(PM_SD_CDZ),
	MSC313_COMMON_PIN(PM_IRIN),
	MSC313_COMMON_PIN(PM_UART_RX),
	MSC313_COMMON_PIN(PM_UART_TX),
	MSC313_COMMON_PIN(PM_GPIO4),
	MSC313_COMMON_PIN(PM_SPI_CZ),
	MSC313_COMMON_PIN(PM_SPI_DI),
	MSC313_COMMON_PIN(PM_SPI_WPZ),
	MSC313_COMMON_PIN(PM_SPI_DO),
	MSC313_COMMON_PIN(PM_SPI_CK),
	MSC313_COMMON_PIN(ETH_RN),
	MSC313_COMMON_PIN(ETH_RP),
	MSC313_COMMON_PIN(ETH_TN),
	MSC313_COMMON_PIN(ETH_TP),
	MSC313_COMMON_PIN(FUART_RX),
	MSC313_COMMON_PIN(FUART_TX),
	MSC313_COMMON_PIN(FUART_CTS),
	MSC313_COMMON_PIN(FUART_RTS),
	MSC313_COMMON_PIN(I2C1_SCL),
	MSC313_COMMON_PIN(I2C1_SDA),
	PINCTRL_PIN(PIN_MSC313_SR_IO2,	"sr_io2"),
	PINCTRL_PIN(PIN_MSC313_SR_IO3,	"sr_io3"),
	PINCTRL_PIN(PIN_MSC313_SR_IO4,	"sr_io4"),
	PINCTRL_PIN(PIN_MSC313_SR_IO5,	"sr_io5"),
	PINCTRL_PIN(PIN_MSC313_SR_IO6,	"sr_io6"),
	PINCTRL_PIN(PIN_MSC313_SR_IO7,	"sr_io7"),
	PINCTRL_PIN(PIN_MSC313_SR_IO8,	"sr_io8"),
	PINCTRL_PIN(PIN_MSC313_SR_IO9,	"sr_io9"),
	PINCTRL_PIN(PIN_MSC313_SR_IO10,	"sr_io10"),
	PINCTRL_PIN(PIN_MSC313_SR_IO11,	"sr_io11"),
	PINCTRL_PIN(PIN_MSC313_SR_IO12,	"sr_io12"),
	PINCTRL_PIN(PIN_MSC313_SR_IO13,	"sr_io13"),
	PINCTRL_PIN(PIN_MSC313_SR_IO14,	"sr_io14"),
	PINCTRL_PIN(PIN_MSC313_SR_IO15,	"sr_io15"),
	PINCTRL_PIN(PIN_MSC313_SR_IO16,	"sr_io16"),
	PINCTRL_PIN(PIN_MSC313_SR_IO17,	"sr_io17"),
	MSC313_COMMON_PIN(SPI0_CZ),
	MSC313_COMMON_PIN(SPI0_CK),
	MSC313_COMMON_PIN(SPI0_DI),
	MSC313_COMMON_PIN(SPI0_DO),
	MSC313_COMMON_PIN(SD_CLK),
	MSC313_COMMON_PIN(SD_CMD),
	MSC313_COMMON_PIN(SD_D0),
	MSC313_COMMON_PIN(SD_D1),
	MSC313_COMMON_PIN(SD_D2),
	MSC313_COMMON_PIN(SD_D3),
	MSC313_COMMON_PIN(USB_DM),
	MSC313_COMMON_PIN(USB_DP),
};

/* mux pin groupings */
static int msc313_pm_uart_pins[]  = { PIN_MSC313_PM_UART_RX, PIN_MSC313_PM_UART_TX };
static int msc313_pm_spi_pins[]   = { PIN_MSC313_PM_SPI_CZ, PIN_MSC313_PM_SPI_DI,
				      PIN_MSC313_PM_SPI_WPZ, PIN_MSC313_PM_SPI_DO,
				      PIN_MSC313_PM_SPI_CK };
static int msc313_eth_pins[]      = { PIN_MSC313_ETH_RN, PIN_MSC313_ETH_RP,
				      PIN_MSC313_ETH_TN, PIN_MSC313_ETH_TP };
static int msc313_fuart_pins[]    = { PIN_MSC313_FUART_RX, PIN_MSC313_FUART_TX,
				      PIN_MSC313_FUART_CTS, PIN_MSC313_FUART_RTS };
static int fuart_rx_pins[]        = { PIN_MSC313_FUART_RX };
static int fuart_tx_pins[]        = { PIN_MSC313_FUART_TX };
static int fuart_cts_pins[]       = { PIN_MSC313_FUART_CTS };
static int fuart_rts_pins[]       = { PIN_MSC313_FUART_RTS };
static int fuart_rx_tx_rts_pins[] = { PIN_MSC313_FUART_RX, PIN_MSC313_FUART_TX, PIN_MSC313_FUART_RTS };
static int fuart_cts_rts_pins[]   = { PIN_MSC313_FUART_CTS, PIN_MSC313_FUART_RTS };
static int msc313_i2c1_pins[]     = { PIN_MSC313_I2C1_SCL, PIN_MSC313_I2C1_SDA };
static int msc313_spi0_pins[]     = { PIN_MSC313_SPI0_CZ, PIN_MSC313_SPI0_CK, PIN_MSC313_SPI0_DI, PIN_MSC313_SPI0_DO };
static int spi0_cz_pins[]         = { PIN_MSC313_SPI0_CZ };
static int spi0_ck_pins[]         = { PIN_MSC313_SPI0_CK };
static int spi0_di_pins[]         = { PIN_MSC313_SPI0_DI };
static int spi0_do_pins[]         = { PIN_MSC313_SPI0_DO };
static int sd_d0_d1_d2_d3_pins[]  = { PIN_MSC313_SD_D0, PIN_MSC313_SD_D1,
		                      PIN_MSC313_SD_D2,PIN_MSC313_SD_D3 };
static int msc313_sd_pins[]       = { PIN_MSC313_SD_CLK, PIN_MSC313_SD_CMD,
		                      PIN_MSC313_SD_D0, PIN_MSC313_SD_D1,
				      PIN_MSC313_SD_D2, PIN_MSC313_SD_D3 };
static int msc313_usb_pins[]	  = { PIN_MSC313_USB_DM, PIN_MSC313_USB_DP };


#define MSC313_PINCTRL_GROUP(_NAME, _name) MSTAR_PINCTRL_GROUP(GROUPNAME_##_NAME, msc313_##_name##_pins)

static const struct mstar_pinctrl_group msc313_pinctrl_groups[] = {
	MSC313_PINCTRL_GROUP(PM_UART, pm_uart),
	MSC313_PINCTRL_GROUP(PM_SPI, pm_spi),
	MSC313_PINCTRL_GROUP(USB, usb),
	MSC313_PINCTRL_GROUP(ETH, eth),
	MSC313_PINCTRL_GROUP(FUART, fuart),
	MSTAR_PINCTRL_GROUP("fuart_rx", fuart_rx_pins),
	MSTAR_PINCTRL_GROUP("fuart_tx", fuart_tx_pins),
	MSTAR_PINCTRL_GROUP("fuart_cts", fuart_cts_pins),
	MSTAR_PINCTRL_GROUP("fuart_rts", fuart_rts_pins),
	MSTAR_PINCTRL_GROUP("fuart_rx_tx_rts", fuart_rx_tx_rts_pins),
	MSTAR_PINCTRL_GROUP("fuart_cts_rts", fuart_cts_rts_pins),
	MSC313_PINCTRL_GROUP(I2C1, i2c1),
	MSC313_PINCTRL_GROUP(SPI0, spi0),
	MSTAR_PINCTRL_GROUP("spi0_cz", spi0_cz_pins),
	MSTAR_PINCTRL_GROUP("spi0_ck", spi0_ck_pins),
	MSTAR_PINCTRL_GROUP("spi0_di", spi0_di_pins),
	MSTAR_PINCTRL_GROUP("spi0_do", spi0_do_pins),
	MSTAR_PINCTRL_GROUP("sd_d0_d1_d2_d3", sd_d0_d1_d2_d3_pins),
	MSC313_PINCTRL_GROUP(SD, sd),
};

/* mux function select values */

static const char* eth_groups[] =     { GROUPNAME_ETH };
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
static const char* sdio_groups[] =    { GROUPNAME_SD };
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

static const struct mstar_pinctrl_function msc313_pinctrl_functions[] = {
	COMMON_FUNCTIONS,
	MSTAR_PINCTRL_FUNCTION("uart0",   0xc,  BIT(5) | BIT(4),  uart0_groups,   NULL),
	MSTAR_PINCTRL_FUNCTION("uart1",   0xc,  BIT(9) | BIT(8),  uart1_groups,   uart1_values),
	MSTAR_PINCTRL_FUNCTION("pwm0",    0x1c, BIT(1) | BIT(0),  pwm0_groups,    pwm0_values),
	MSTAR_PINCTRL_FUNCTION("pwm1",    0x1c, BIT(3) | BIT(2),  pwm1_groups,    pwm1_values),
	MSTAR_PINCTRL_FUNCTION("pwm2",    0x1c, BIT(5) | BIT(4),  pwm2_groups,    pwm2_values),
	MSTAR_PINCTRL_FUNCTION("pwm3",    0x1c, BIT(7) | BIT(6),  pwm3_groups,    pwm3_values),
	MSTAR_PINCTRL_FUNCTION("pwm4",    0x1c, BIT(9) | BIT(8),  pwm4_groups,    pwm4_values),
	MSTAR_PINCTRL_FUNCTION("pwm5",    0x1c, BIT(11)| BIT(10), pwm5_groups,    pwm5_values),
	MSTAR_PINCTRL_FUNCTION("pwm6",    0x1c, BIT(13)| BIT(11), pwm6_groups,    pwm6_values),
	MSTAR_PINCTRL_FUNCTION("pwm7",    0x1c, BIT(15)| BIT(14), pwm7_groups,    pwm7_values),
	MSTAR_PINCTRL_FUNCTION("sdio",    0x20, BIT(8),           sdio_groups,    sdio_values),
	MSTAR_PINCTRL_FUNCTION("i2c1",    0x24, BIT(5) | BIT(4),  i2c1_groups,    i2c1_values),
	MSTAR_PINCTRL_FUNCTION("spi0",	  0x30, BIT(1) | BIT(0),  spi0_groups,    spi0_values),
	MSTAR_PINCTRL_FUNCTION("spi1",	  0x30, BIT(5) | BIT(4),  spi1_groups,    spi1_values),
	MSTAR_PINCTRL_FUNCTION("jtag",    0x3c, BIT(1) | BIT(0),  jtag_groups,    NULL),
	MSTAR_PINCTRL_FUNCTION("eth",     0x3c, BIT(2),           eth_groups,     eth_values),
};

#define MSTAR_PINCTRL_PIN(_pin, _pullupreg, _pullupenbit, _drivereg, _drivebit, _drivecurrents) \
	{ \
		.pin = _pin, \
		.pullupreg = _pullupreg, \
		.pullupenbit = _pullupenbit, \
		.drivereg = _drivereg, \
		.drivebit = _drivebit, \
		.drivecurrents = _drivecurrents, \
		.ndrivecurrents = ARRAY_SIZE(_drivecurrents) \
	}

#define REG_SDIO_PULLDRIVE 0xc8
static const unsigned sd_drivestrenghts[] = {4, 8};

static const struct mstar_configurable_pin msc313_configurable_pins[] = {
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_CMD, REG_SDIO_PULLDRIVE, 8, REG_SDIO_PULLDRIVE, 0, sd_drivestrenghts),
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_D0, REG_SDIO_PULLDRIVE, 9, REG_SDIO_PULLDRIVE, 1, sd_drivestrenghts),
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_D1, REG_SDIO_PULLDRIVE, 10, REG_SDIO_PULLDRIVE, 2, sd_drivestrenghts),
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_D2, REG_SDIO_PULLDRIVE, 11, REG_SDIO_PULLDRIVE, 3, sd_drivestrenghts),
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_D3, REG_SDIO_PULLDRIVE, 12, REG_SDIO_PULLDRIVE, 4, sd_drivestrenghts),
	MSTAR_PINCTRL_PIN(PIN_MSC313_SD_CLK, -1, -1, REG_SDIO_PULLDRIVE, 5, sd_drivestrenghts),
};

#define MSTAR_PINCTRL_INFO(_chip) static const struct mstar_pinctrl_info _chip##_info = { \
	.pins = _chip##_pins, \
	.npins = ARRAY_SIZE(_chip##_pins), \
	.groups = _chip##_pinctrl_groups, \
	.ngroups = ARRAY_SIZE(_chip##_pinctrl_groups), \
	.functions = _chip##_pinctrl_functions, \
	.nfunctions = ARRAY_SIZE(_chip##_pinctrl_functions), \
	.confpin = _chip##_configurable_pins, \
	.nconfpins = ARRAY_SIZE(_chip##_configurable_pins),\
}


MSTAR_PINCTRL_INFO(msc313);

/* SSC8336 */
/* Chip pin numbers */
#define PIN_SSC8336N_USB_DM1     7
#define PIN_SSC8336N_USB_DP1     8
#define PIN_SSC8336N_USB_DM      9
#define PIN_SSC8336N_USB_DP      10
#define PIN_SSC8336N_USB_CID     12
#define PIN_SSC8336N_PM_SPI_CZ   27
#define PIN_SSC8336N_PM_SPI_DI   28
#define PIN_SSC8336N_PM_SPI_WPZ  29
#define PIN_SSC8336N_PM_SPI_DO   30
#define PIN_SSC8336N_PM_SPI_CK   31
#define PIN_SSC8336N_PM_SPI_HOLD 32
#define PIN_SSC8336N_PM_GPIO8    34
#define PIN_SSC8336N_PM_GPIO6	 35
#define PIN_SSC8336N_PM_GPIO5	 36
#define PIN_SSC8336N_PM_GPIO4	 37
#define PIN_SSC8336N_PM_GPIO2	 38
#define PIN_SSC8336N_PM_GPIO0	 39
#define PIN_SSC8336N_PM_UART_TX	 40
#define PIN_SSC8336N_PM_UART_RX	 41
#define PIN_SSC8336N_PM_IRIN	 42
#define PIN_SSC8336N_PM_SD_CDZ	 43
#define PIN_SSC8336N_FUART_RX	 52
#define PIN_SSC8336N_FUART_TX	 53
#define PIN_SSC8336N_FUART_CTS	 54
#define PIN_SSC8336N_FUART_RTS	 55
#define PIN_SSC8336N_SPI0_DO	 56
#define PIN_SSC8336N_SPI0_DI	 57
#define PIN_SSC8336N_SPI0_CK	 58
#define PIN_SSC8336N_SPI0_CZ	 59
#define PIN_SSC8336N_SPI0_CZ1	 60
#define PIN_SSC8336N_I2C0_SCL	 61
#define PIN_SSC8336N_I2C0_SDA	 62
#define PIN_SSC8336N_SD_D1	 67
#define PIN_SSC8336N_SD_D0	 68
#define PIN_SSC8336N_SD_CLK	 69
#define PIN_SSC8336N_SD_CMD	 70
#define PIN_SSC8336N_SD_D3	 71
#define PIN_SSC8336N_SD_D2	 72
#define PIN_SSC8336N_SR0_D2	 73
#define PIN_SSC8336N_SR0_D3	 74
#define PIN_SSC8336N_SR0_D4	 75
#define PIN_SSC8336N_SR0_D5	 76
#define PIN_SSC8336N_SR0_D6	 77
#define PIN_SSC8336N_SR0_D7	 78
#define PIN_SSC8336N_SR0_D8	 79
#define PIN_SSC8336N_SR0_D9	 80
#define PIN_SSC8336N_SR0_D10	 81
#define PIN_SSC8336N_SR0_D11	 82
#define PIN_SSC8336N_I2C1_SCL	 84
#define PIN_SSC8336N_I2C1_SDA	 85
#define PIN_SSC8336N_SR0_GPIO2	 86
#define PIN_SSC8336N_SR0_GPIO3	 87
#define PIN_SSC8336N_SR0_GPIO4	 88
#define PIN_SSC8336N_SR0_GPIO5	 89
#define PIN_SSC8336N_SR0_GPIO6	 90
#define PIN_SSC8336N_SR1_GPIO0   92
#define PIN_SSC8336N_SR1_GPIO1   93
#define PIN_SSC8336N_SR1_GPIO2   94
#define PIN_SSC8336N_SR1_GPIO3   95
#define PIN_SSC8336N_SR1_GPIO4   96
#define PIN_SSC8336N_SR1_D0P     97
#define PIN_SSC8336N_SR1_D1N     98
#define PIN_SSC8336N_SR1_CKP     99
#define PIN_SSC8336N_SR1_CKN     100
#define PIN_SSC8336N_SR1_D1P     101
#define PIN_SSC8336N_SR1_D1N     102
#define PIN_SSC8336N_LCD_HSYNC   104
#define PIN_SSC8336N_LCD_VSYNC   105
#define PIN_SSC8336N_LCD_PCLK    106
#define PIN_SSC8336N_LCD_DE      107
#define PIN_SSC8336N_LCD_0       108
#define PIN_SSC8336N_LCD_1       109
#define PIN_SSC8336N_LCD_2       110
#define PIN_SSC8336N_LCD_3       111
#define PIN_SSC8336N_LCD_4       112
#define PIN_SSC8336N_LCD_5       113
#define PIN_SSC8336N_LCD_6       114
#define PIN_SSC8336N_LCD_7       115
#define PIN_SSC8336N_LCD_8       116
#define PIN_SSC8336N_LCD_9       117
#define PIN_SSC8336N_LCD_10      119
#define PIN_SSC8336N_LCD_11      120
#define PIN_SSC8336N_LCD_12      121
#define PIN_SSC8336N_LCD_13      122
#define PIN_SSC8336N_LCD_14      123
#define PIN_SSC8336N_LCD_15      124

/* pinctrl pins */
#define SSC8336N_COMMON_PIN(_pinname) COMMON_PIN(SSC8336N, _pinname)

static struct pinctrl_pin_desc ssc8336n_pins[] = {
	SSC8336N_COMMON_PIN(USB_DM1),
	SSC8336N_COMMON_PIN(USB_DP1),
	SSC8336N_COMMON_PIN(USB_DM),
	SSC8336N_COMMON_PIN(USB_DP),
	SSC8336N_COMMON_PIN(USB_CID),
	SSC8336N_COMMON_PIN(PM_SPI_CZ),
	SSC8336N_COMMON_PIN(PM_SPI_DI),
	SSC8336N_COMMON_PIN(PM_SPI_WPZ),
	SSC8336N_COMMON_PIN(PM_SPI_DO),
	SSC8336N_COMMON_PIN(PM_SPI_CK),
	SSC8336N_COMMON_PIN(PM_SPI_HOLD),
	SSC8336N_COMMON_PIN(PM_GPIO8),
	SSC8336N_COMMON_PIN(PM_GPIO6),
	SSC8336N_COMMON_PIN(PM_GPIO5),
	SSC8336N_COMMON_PIN(PM_GPIO4),
	SSC8336N_COMMON_PIN(PM_GPIO2),
	SSC8336N_COMMON_PIN(PM_GPIO0),
	SSC8336N_COMMON_PIN(PM_UART_TX),
	SSC8336N_COMMON_PIN(PM_UART_RX),
	SSC8336N_COMMON_PIN(PM_IRIN),
	SSC8336N_COMMON_PIN(PM_SD_CDZ),
	SSC8336N_COMMON_PIN(FUART_RX),
	SSC8336N_COMMON_PIN(FUART_TX),
	SSC8336N_COMMON_PIN(FUART_CTS),
	SSC8336N_COMMON_PIN(FUART_RTS),
	SSC8336N_COMMON_PIN(SPI0_DO),
	SSC8336N_COMMON_PIN(SPI0_DI),
	SSC8336N_COMMON_PIN(SPI0_CK),
	SSC8336N_COMMON_PIN(SPI0_CZ),
	SSC8336N_COMMON_PIN(SPI0_CZ1),
	SSC8336N_COMMON_PIN(I2C0_SCL),
	SSC8336N_COMMON_PIN(I2C0_SDA),
	SSC8336N_COMMON_PIN(SD_D1),
	SSC8336N_COMMON_PIN(SD_D0),
	SSC8336N_COMMON_PIN(SD_CLK),
	SSC8336N_COMMON_PIN(SD_CMD),
	SSC8336N_COMMON_PIN(SD_D3),
	SSC8336N_COMMON_PIN(SD_D2),
	SSC8336N_COMMON_PIN(I2C1_SCL),
	SSC8336N_COMMON_PIN(I2C1_SDA),
	SSC8336N_COMMON_PIN(LCD_HSYNC),
	SSC8336N_COMMON_PIN(LCD_VSYNC),
	SSC8336N_COMMON_PIN(LCD_PCLK),
	SSC8336N_COMMON_PIN(LCD_DE),
	SSC8336N_COMMON_PIN(LCD_0),
	SSC8336N_COMMON_PIN(LCD_1),
	SSC8336N_COMMON_PIN(LCD_2),
	SSC8336N_COMMON_PIN(LCD_3),
	SSC8336N_COMMON_PIN(LCD_4),
	SSC8336N_COMMON_PIN(LCD_5),
	SSC8336N_COMMON_PIN(LCD_6),
	SSC8336N_COMMON_PIN(LCD_7),
	SSC8336N_COMMON_PIN(LCD_8),
	SSC8336N_COMMON_PIN(LCD_9),
	SSC8336N_COMMON_PIN(LCD_10),
	SSC8336N_COMMON_PIN(LCD_11),
	SSC8336N_COMMON_PIN(LCD_12),
	SSC8336N_COMMON_PIN(LCD_13),
	SSC8336N_COMMON_PIN(LCD_14),
	SSC8336N_COMMON_PIN(LCD_15),
};

/* mux pin groupings */
static int ssc8336n_pm_uart_pins[] = { PIN_SSC8336N_PM_UART_TX, PIN_SSC8336N_PM_UART_RX };
static int ssc8336n_pm_spi_pins[]  = { PIN_SSC8336N_PM_SPI_CZ, PIN_SSC8336N_PM_SPI_CZ,
				       PIN_SSC8336N_PM_SPI_DI, PIN_SSC8336N_PM_SPI_WPZ,
				       PIN_SSC8336N_PM_SPI_DO, PIN_SSC8336N_PM_SPI_CK,
				       PIN_SSC8336N_PM_SPI_HOLD };
static int ssc8336n_i2c0_pins[]    = { PIN_SSC8336N_I2C0_SCL, PIN_SSC8336N_I2C0_SDA };
static int ssc8336n_i2c1_pins[]    = { PIN_SSC8336N_I2C1_SCL, PIN_SSC8336N_I2C1_SDA };
static int ssc8336n_usb_pins[]     = { PIN_SSC8336N_USB_DM, PIN_SSC8336N_USB_DP };
static int ssc8336n_usb1_pins[]    = { PIN_SSC8336N_USB_DM1, PIN_SSC8336N_USB_DP1 };
static int ssc8336n_sd_pins[]      = { PIN_SSC8336N_SD_CLK, PIN_SSC8336N_SD_CMD,
				       PIN_SSC8336N_SD_D0, PIN_SSC8336N_SD_D1,
				       PIN_SSC8336N_SD_D2, PIN_SSC8336N_SD_D3 };
static int ssc8336n_fuart_pins[]   = { PIN_SSC8336N_FUART_RX, PIN_SSC8336N_FUART_TX,
				       PIN_SSC8336N_FUART_CTS, PIN_SSC8336N_FUART_RTS };

#define SSC8336N_PINCTRL_GROUP(_NAME, _name) MSTAR_PINCTRL_GROUP(GROUPNAME_##_NAME, ssc8336n_##_name##_pins)

/* pinctrl groups */
static const struct mstar_pinctrl_group ssc8336n_pinctrl_groups[] = {
	SSC8336N_PINCTRL_GROUP(PM_UART,pm_uart),
	SSC8336N_PINCTRL_GROUP(PM_SPI,pm_spi),
	SSC8336N_PINCTRL_GROUP(I2C0,i2c0),
	SSC8336N_PINCTRL_GROUP(I2C1,i2c1),
	SSC8336N_PINCTRL_GROUP(USB,usb),
	SSC8336N_PINCTRL_GROUP(USB1,usb1),
	SSC8336N_PINCTRL_GROUP(SD,sd),
	SSC8336N_PINCTRL_GROUP(FUART,fuart),
};

static const struct mstar_pinctrl_function ssc8336n_pinctrl_functions[] = {
	COMMON_FUNCTIONS,
	COMMON_FIXED_FUNCTION(USB1, usb1),
	MSTAR_PINCTRL_FUNCTION("uart0",   0xc,  BIT(5) | BIT(4),  uart0_groups,   NULL),
	MSTAR_PINCTRL_FUNCTION("uart1",   0xc,  BIT(9) | BIT(8),  uart1_groups,   uart1_values),
	MSTAR_PINCTRL_FUNCTION("pwm0",    0x1c, BIT(1) | BIT(0),  pwm0_groups,    pwm0_values),
	MSTAR_PINCTRL_FUNCTION("pwm1",    0x1c, BIT(3) | BIT(2),  pwm1_groups,    pwm1_values),
	MSTAR_PINCTRL_FUNCTION("pwm2",    0x1c, BIT(5) | BIT(4),  pwm2_groups,    pwm2_values),
	MSTAR_PINCTRL_FUNCTION("pwm3",    0x1c, BIT(7) | BIT(6),  pwm3_groups,    pwm3_values),
	MSTAR_PINCTRL_FUNCTION("pwm4",    0x1c, BIT(9) | BIT(8),  pwm4_groups,    pwm4_values),
	MSTAR_PINCTRL_FUNCTION("pwm5",    0x1c, BIT(11)| BIT(10), pwm5_groups,    pwm5_values),
	MSTAR_PINCTRL_FUNCTION("pwm6",    0x1c, BIT(13)| BIT(11), pwm6_groups,    pwm6_values),
	MSTAR_PINCTRL_FUNCTION("pwm7",    0x1c, BIT(15)| BIT(14), pwm7_groups,    pwm7_values),
	MSTAR_PINCTRL_FUNCTION("sdio",    0x20, BIT(8),           sdio_groups,    sdio_values),
	MSTAR_PINCTRL_FUNCTION("i2c0",    0x24, BIT(1) | BIT(0),  i2c0_groups,    i2c0_values),
	MSTAR_PINCTRL_FUNCTION("i2c1",    0x24, BIT(5) | BIT(4),  i2c1_groups,    i2c1_values),
	MSTAR_PINCTRL_FUNCTION("spi0",	  0x30, BIT(1) | BIT(0),  spi0_groups,    spi0_values),
	MSTAR_PINCTRL_FUNCTION("spi1",	  0x30, BIT(5) | BIT(4),  spi1_groups,    spi1_values),
	MSTAR_PINCTRL_FUNCTION("jtag",    0x3c, BIT(1) | BIT(0),  jtag_groups,    NULL),
	MSTAR_PINCTRL_FUNCTION("eth",     0x3c, BIT(2),           eth_groups,     eth_values),
};

static const struct mstar_pinctrl_info ssc8336n_info = {
	.pins = ssc8336n_pins,
	.npins = ARRAY_SIZE(ssc8336n_pins),
	.groups  = ssc8336n_pinctrl_groups,
	.ngroups = ARRAY_SIZE(ssc8336n_pinctrl_groups),
	.functions = ssc8336n_pinctrl_functions,
	.nfunctions = ARRAY_SIZE(ssc8336n_pinctrl_functions),
};

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
	struct mstar_pinctrl_function *function = funcdesc->data;
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

static int mstar_pinctrl_parse_groups(struct mstar_pinctrl *pinctrl){
	int i, ret;
	for(i = 0; i < pinctrl->info->ngroups; i++){
		const struct mstar_pinctrl_group *grp = &pinctrl->info->groups[i];
		ret = pinctrl_generic_add_group(pinctrl->pctl, grp->name,
				grp->pins, grp->numpins, NULL);
	}
	return ret;
}

static int mstar_pinctrl_parse_functions(struct mstar_pinctrl *pinctrl){
	int i, ret;
	for(i = 0; i < pinctrl->info->nfunctions; i++){
		const struct mstar_pinctrl_function *func =  &pinctrl->info->functions[i];

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
	const struct mstar_configurable_pin *confpin;
	dev_dbg(pinctrl->dev, "setting %d:%u on pin %d\n", (int)config,(unsigned)arg, pin);
	for(i = 0; i < pinctrl->info->nconfpins; i++){
		if(pinctrl->info->confpin[i].pin == pin){
			confpin = &pinctrl->info->confpin[i];
			switch(param){
			case PIN_CONFIG_BIAS_PULL_UP:
				if(confpin->pullupreg != -1){
					dev_dbg(pinctrl->dev, "setting pull up %d on pin %d\n", (int) arg, pin);
					mask = 1 << confpin->pullupenbit;
					regmap_update_bits(pinctrl->regmap, confpin->pullupreg, mask, arg ? mask : 0);
				}
				else
					dev_info(pinctrl->dev, "pullup reg/bit isn't known for pin %d\n", pin);
			default:
				break;
			}
			return 0;
		}
	}
	return 0;
}

static int mstar_pinctrl_get_config(struct mstar_pinctrl *pinctrl, int pin, unsigned long *config){
	int i;
	const struct mstar_configurable_pin *confpin;
	unsigned val;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned crntidx;

	/* we only support a limited range of conf options so filter the
	 * ones we do here
	 */

	switch(param){
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_DRIVE_STRENGTH:
			break;
		default:
			goto out;
	}

	/* try to find the configuration register(s) for the pin */
	for(i = 0; i < pinctrl->info->nconfpins; i++){
		if(pinctrl->info->confpin[i].pin == pin){
			confpin = &pinctrl->info->confpin[i];
			switch(param){
				case PIN_CONFIG_BIAS_PULL_UP:
					if(confpin->pullupreg != -1){
						regmap_read(pinctrl->regmap, confpin->pullupreg, &val);
						return val & BIT(confpin->pullupenbit) ? 0 : -EINVAL;
					}
					else
						goto out;
				case PIN_CONFIG_DRIVE_STRENGTH:
					if(confpin->drivereg != -1){
						regmap_read(pinctrl->regmap, confpin->drivereg, &val);
						crntidx = (val & BIT(confpin->drivebit)) >> confpin->drivebit;
						*config = pinconf_to_config_packed(param, confpin->drivecurrents[crntidx]);
						return 0;
					}
					else
						goto out;
				default:
					goto out;
			}
		}
	}

out:
	return -ENOTSUPP;
}

int mstar_pin_config_get(struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *config){
	struct mstar_pinctrl *pinctrl = pctldev->driver_data;
	return mstar_pinctrl_get_config(pinctrl, pin, config);
}

int mstar_pin_config_set(struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *configs,
			       unsigned num_configs){
	int i;
	struct mstar_pinctrl *pinctrl = pctldev->driver_data;
	for(i = 0; i < num_configs; i++){
		mstar_set_config(pinctrl, pin, configs[i]);
	}
	return 0;
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
	const struct mstar_pinctrl_info *match_data;

	match_data = of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return -EINVAL;

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pinctrl);

	pinctrl->dev = &pdev->dev;
	pinctrl->info = match_data;

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
	pinctrl->desc.pins = pinctrl->info->pins;
	pinctrl->desc.npins = pinctrl->info->npins;

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
		.compatible	= "mstar,msc313-pinctrl",
		.data		= &msc313_info,
	},
	{
		.compatible	= "mstar,msc313e-pinctrl",
		.data		= &msc313_info,
	},
	{
		.compatible	= "mstar,ssc8336-pinctrl",
		.data		= &ssc8336n_info,
	},
	{
		.compatible	= "mstar,ssc8336n-pinctrl",
		.data		= &ssc8336n_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mstar_pinctrl_of_match);

static struct platform_driver mstar_pinctrl_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = mstar_pinctrl_of_match,
	},
	.probe = mstar_pinctrl_probe,
};

module_platform_driver(mstar_pinctrl_driver);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("Pin controller driver for MStar SoCs");
MODULE_LICENSE("GPL v2");
