#ifndef __DT_BINDINGS_RZA1PFC_PINCTRL_H__
#define __DT_BINDINGS_RZA1PFC_PINCTRL_H__

#define RZA1PFC_PORT_SHIFT 4

#define RZA1PFC_PIN(port, pin) ((port << RZA1PFC_PORT_SHIFT) | pin)

#define RZA1PFC_MODE_GPIO	0
#define RZA1PFC_MODE_ALT1	1
#define RZA1PFC_MODE_ALT2	2
#define RZA1PFC_MODE_ALT3	3
#define RZA1PFC_MODE_ALT4	4
#define RZA1PFC_MODE_ALT5	5
#define RZA1PFC_MODE_ALT6	6
#define RZA1PFC_MODE_ALT7	7
#define RZA1PFC_MODE_ALT8	8


#define RZA1PFC_DIIO_PBDC_DIS		0	/* Direct I/O Mode & PBDC Disable */
#define RZA1PFC_DIIO_PBDC_EN		1	/* Direct I/O Mode & PBDC Enable */
#define	RZA1PFC_SWIO_OUT_PBDCDIS	2	/* Software I/O Mode & Output direction PBDC Disable */
#define	RZA1PFC_SWIO_OUT_PBDCEN		3	/* Software I/O Mode & Output direction PBDC Enable */
#define RZA1PFC_PORT_OUT_HIGH		4	/* Port Mode & Output direction & High Level Output Pn = 1 */
#define RZA1PFC_PORT_OUT_LOW		5	/* Port Mode & Output direction & Low Level Output Pn = 0 */
#define RZA1PFC_DIR_OUT				6
#define RZA1PFC_DIR_IN				7	/* Port Mode or Software I/O Mode is Direction IN */
#define RZA1PFC_DIR_LVDS			8


#endif /* __DT_BINDINGS_RZA1PFC_PINCTRL_H__ */
