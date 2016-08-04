#ifndef __DT_BINDINGS_RZA1_DMA_H__
#define __DT_BINDINGS_RZA1_DMA_H__

/*
#define	CHCFG_TM(bit)		(bit << 22)
#define	CHCFG_DDS(bit)		(bit << 16)
#define	CHCFG_SDS(bit)		(bit << 12)
#define	CHCFG_AM(bits)		(bits << 8)
#define	CHCFG_LVL(bit)		(bit << 6)
#define	CHCFG_HIEN(bit)		(bit << 5)
#define	CHCFG_LOEN(bit)		(bit << 4)
#define	CHCFG_REQD(bit)		(bit << 3)

// DMARS
#define	DMARS_RID(bit)		(bit << 0)
#define	DMARS_MID(bit)		(bit << 2)

*/

#define	RZA1DMA_CHCFG_8BIT		0x00
#define	RZA1DMA_CHCFG_16BIT		0x01
#define	RZA1DMA_CHCFG_32BIT		0x02
#define	RZA1DMA_CHCFG_64BIT		0x03
#define RZA1DMA_CHCFG_128BIT	0x04
#define	RZA1DMA_CHCFG_256BIT	0x05
#define	RZA1DMA_CHCFG_512BIT	0x06
#define	RZA1DMA_CHCFG_1024BIT	0x07

#define RZA1DMA_CHCFGM(reqd_v, loen_v, hien_v, lvl_v, am_v, sds_v, dds_v, tm_v)\
 ((reqd_v << 3) | (loen_v << 4) | (hien_v << 5) | (lvl_v << 6) |\
 (am_v << 8) | (sds_v << 12) | (dds_v << 16 ) | (tm_v << 22))

#define RZA1DMA_DMARS(rid_v, mid_v) (rid_v | (mid_v << 2))

#define RZA1DMA_SPI0 0x48
#define RZA1DMA_SPI1 0x49
#define RZA1DMA_SPI2 0x4A
#define RZA1DMA_SPI3 0x4B
#define RZA1DMA_SPI4 0x4C

#define RZA1DMA_REQD_SOURCE			0
#define RZA1DMA_REQD_DESTINATION	1

#define RZA1DMA_AMA_BUSCYCLEMODE	2
#define RZA1DMA_AMA_DMAACK			4

#endif /* __DT_BINDINGS_RZA1_DMA_H__ */
