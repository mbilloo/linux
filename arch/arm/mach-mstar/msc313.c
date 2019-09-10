// SPDX-License-Identifier: GPL-2.0
/*
 * Device Tree support for MStar MSC313 SoCs
 *
 * Copyright (c) 2019 thingy.jp
 * Author: Daniel Palmer <daniel@thingy.jp>
 */

#include <linux/init.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/of.h>
#include <asm/io.h>

/*
 * The IO space is remapped to the same place
 * the vendor kernel does so that the hardcoded
 * addresses all over the vendor drivers line up.
 */

#define MSC313_IO_PHYS		0x1f000000
#define MSC313_IO_OFFSET	0xde000000
#define MSC313_IO_VIRT		(MSC313_IO_PHYS + MSC313_IO_OFFSET)
#define MSC313_IO_SIZE		0x00400000

/*
 * In the u-boot code the area these registers are in is
 * called "L3 bridge".
 *
 * It's not exactly known what is the L3 bridge is but
 * the vendor code for both u-boot and linux share calls
 * to "flush the miu pipe". This seems to be to force pending
 * CPU writes to memory so that the state is right before
 * DMA capable devices try to read descriptors and data
 * the CPU has prepared. Without doing this ethernet doesn't
 * work reliably for example.
 */

#define MSC313_L3BRIDGE_FLUSH		0x204414
#define MSC313_L3BRIDGE_STATUS		0x204440
#define MSC313_L3BRIDGE_FLUSH_TRIGGER	BIT(0)
#define MSC313_L3BRIDGE_STATUS_DONE	BIT(12)

static void __iomem *miu_status;
static void __iomem *miu_flush;

static struct map_desc msc313_io_desc[] __initdata = {
		{MSC313_IO_VIRT, __phys_to_pfn(MSC313_IO_PHYS),
				MSC313_IO_SIZE, MT_DEVICE},
};

static void __init msc313_map_io(void)
{
	iotable_init(msc313_io_desc, ARRAY_SIZE(msc313_io_desc));
	miu_flush = (void __iomem *) (msc313_io_desc[0].virtual
			+ MSC313_L3BRIDGE_FLUSH);
	miu_status = (void __iomem *) (msc313_io_desc[0].virtual
			+ MSC313_L3BRIDGE_STATUS);
}

static const char * const msc313_board_dt_compat[] = {
	"mstar,msc313",
	NULL,
};

static DEFINE_SPINLOCK(msc313_mb_lock);

static void msc313_mb(void)
{
	unsigned long flags;

	spin_lock_irqsave(&msc313_mb_lock, flags);
	/* toggle the flush miu pipe fire bit */
	writel_relaxed(0, miu_flush);
	writel_relaxed(MSC313_L3BRIDGE_FLUSH_TRIGGER, miu_flush);
	while (!(readl_relaxed(miu_status) & MSC313_L3BRIDGE_STATUS_DONE)) {
		/* wait for flush to complete */
	}
	spin_unlock_irqrestore(&msc313_mb_lock, flags);
}

static void __init msc313_barriers_init(void)
{
	soc_mb = msc313_mb;
}

static void __init msc313_init(void)
{
	msc313_barriers_init();
}

DT_MACHINE_START(MSTAR_DT, "MStar MSC313 (Device Tree)")
	.dt_compat	= msc313_board_dt_compat,
	.init_machine = msc313_init,
	.map_io = msc313_map_io,
MACHINE_END
