// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mtd/spi-nor.h>

#define REG_PASSWORD				0x0
#define VAL_PASSWORD_UNLOCK			0xAAAA
#define VAL_PASSWORD_LOCK			0x5555
#define REG_SPI_WDATA				0x10
#define REG_SPI_RDATA				0x14
#define REG_SPI_CECLR				0x20
#define BIT_SPI_CECLR_CLEAR			BIT(0)
#define REG_SPI_RDREQ				0x30
#define BIT_SPI_RDREQ_REQ			BIT(0)
#define REG_SPI_RD_DATARDY			0x54
#define BIT_SPI_RD_DATARDY_READY	BIT(0)
#define REG_SPI_WR_DATARDY			0x58
#define BIT_SPI_WR_DATARDY_READY	BIT(0)
#define REG_TRIGGER_MODE			0xa8
#define VAL_TRIGGER_MODE_ENABLE		0x3333
#define VAL_TRIGGER_MODE_DISABLE	0x2222

//unused
#define REG_SPI_COMMAND				(0x1 * REG_OFFSET)
#define REG_SPI_ADDR_L				(0x2 * REG_OFFSET)
#define REG_SPI_ADDR_H				(0x3 * REG_OFFSET)

static struct msc313e_spinor {
	void __iomem *base;
	void __iomem *memorymapped;
	struct spi_nor nor;
};

static void msc313e_spinor_spi_transaction_start(struct msc313e_spinor *spinor){
	printk("unlock\n");
	iowrite16(VAL_PASSWORD_UNLOCK, spinor->base + REG_PASSWORD);
	iowrite16(VAL_TRIGGER_MODE_ENABLE, spinor->base + REG_TRIGGER_MODE);
}

static void msc313e_spinor_spi_writebyte(struct msc313e_spinor *spinor, u8 value){
	iowrite8(value, spinor->base + REG_SPI_WDATA);
	printk("waiting for write to return\n");
	while(!(ioread8(spinor->base + REG_SPI_WR_DATARDY) & BIT_SPI_WR_DATARDY_READY)){

	}
}

static void msc313e_spinor_spi_readbyte(struct msc313e_spinor *spinor, u8 *dest){
	printk("waiting for read to return\n");
	iowrite8(BIT_SPI_RDREQ_REQ, spinor->base + REG_SPI_RDREQ);
	while(!(ioread8(spinor->base + REG_SPI_RD_DATARDY) & BIT_SPI_RD_DATARDY_READY)){

	}
	*dest = ioread8(spinor->base + REG_SPI_RDATA);
	printk("reg - %02x\n", (unsigned) *dest);
}

static void msc313e_spinor_spi_transaction_end(struct msc313e_spinor *spinor){
	printk("lock\n");
	iowrite8(BIT_SPI_CECLR_CLEAR, spinor->base + REG_SPI_CECLR);
	iowrite16(VAL_TRIGGER_MODE_DISABLE, spinor->base + REG_TRIGGER_MODE);
	iowrite16(VAL_PASSWORD_LOCK, spinor->base + REG_PASSWORD);
}

static int msc313e_spinor_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len){
	struct msc313e_spinor *spinor = nor->priv;
	int i;
	msc313e_spinor_spi_transaction_start(spinor);
	msc313e_spinor_spi_writebyte(spinor, opcode);
	for(i = 0; i < len; i++)
		msc313e_spinor_spi_readbyte(spinor, buf++);
	msc313e_spinor_spi_transaction_end(spinor);
	return 0;
};

static int msc313e_spinor_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len){
	struct msc313e_spinor *spinor = nor->priv;
	int i;
	msc313e_spinor_spi_transaction_start(spinor);
	msc313e_spinor_spi_writebyte(spinor, opcode);
	for(i = 0; i < len; i++)
		msc313e_spinor_spi_writebyte(spinor, *buf++);
	msc313e_spinor_spi_transaction_end(spinor);
	return 0;
}

static ssize_t msc313e_spinor_read(struct spi_nor *nor, loff_t from,
		size_t len, u_char *read_buf){
	struct msc313e_spinor *spinor = nor->priv;
	memcpy_fromio(read_buf, spinor->memorymapped + from, len);
	return len;
}

static ssize_t msc313e_spinor_write(struct spi_nor *nor, loff_t to,
			size_t len, const u_char *write_buf){
	printk("write\n");
	return 0;
}

static const struct spi_nor_hwcaps msc313e_spinor_hwcaps = {
		.mask = SNOR_HWCAPS_READ | SNOR_HWCAPS_PP
};

static int msc313e_spinor_probe(struct platform_device *pdev)
{
	struct msc313e_spinor *spinor;
	struct spi_nor *nor;
	int ret;

	printk("spi nor probe\n");

	spinor = devm_kzalloc(&pdev->dev, sizeof(*spinor), GFP_KERNEL);
		if (!spinor)
			return -ENOMEM;

	spinor->base = of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR(spinor->base))
		return PTR_ERR(spinor->base);

	spinor->memorymapped = of_iomap(pdev->dev.of_node, 1);
		if (IS_ERR(spinor->memorymapped))
			return PTR_ERR(spinor->memorymapped);

	nor = &spinor->nor;

	spi_nor_set_flash_node(nor, pdev->dev.of_node);
	nor->dev = &pdev->dev;
	nor->read_reg = msc313e_spinor_read_reg;
	nor->write_reg = msc313e_spinor_write_reg;
	nor->read = msc313e_spinor_read;
	nor->write = msc313e_spinor_write;
	nor->priv = spinor;

	ret = spi_nor_scan(nor, NULL, &msc313e_spinor_hwcaps);

	ret = mtd_device_register(&nor->mtd, NULL, 0);

	return ret;
}

static int msc313e_spinor_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id msc313e_spinor_match[] = {
	{.compatible = "mstar,msc313e-spinor"},
	{}
};
MODULE_DEVICE_TABLE(of, msc313e_spinor_match);

static struct platform_driver msc313e_spinor_driver = {
	.probe	= msc313e_spinor_probe,
	.remove	= msc313e_spinor_remove,
	.driver	= {
		.name = "msc313e-spinor",
		.of_match_table = msc313e_spinor_match,
	},
};
module_platform_driver(msc313e_spinor_driver);

MODULE_AUTHOR("Daniel Palmer <daniel@0x0f.com>");
MODULE_DESCRIPTION("MStar MSC313e spi-nor driver");
MODULE_LICENSE("GPL v2");
