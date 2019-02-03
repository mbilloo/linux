/*
 * Device Tree support for MStar SoCs
 *
 * Copyright (c) 2017 thingy.jp
 * Author: Daniel Palmer <daniel@thingy.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <asm/mach/arch.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>

static const char * const mstar_board_dt_compat[] = {
	"mstar,msc313e",
	NULL,
};

DT_MACHINE_START(MSTAR_DT, "MStar Cortex-A7 (Device Tree)")
	.dt_compat	= mstar_board_dt_compat,
MACHINE_END
