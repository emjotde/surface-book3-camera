/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IPU4P (Ice Lake) csi2 receiver registers, from the intel 4.19 tree
 * (ipu4/ipu-platform-isys-csi2-reg.h, CONFIG_VIDEO_INTEL_IPU4P branch).
 */

#ifndef IPU4P_PLATFORM_ISYS_CSI2_REG_H
#define IPU4P_PLATFORM_ISYS_CSI2_REG_H

#include <linux/bits.h>

#define IPU4P_GPOFFSET				0x66800
#define IPU4P_COMBO_GPOFFSET			0x6e800

/*
 * Per-port csi2 rx register base, relative to the ISYS base.
 * Available ports: s0p3 (port 0), s1p0..s1p3 (ports 1..4).
 */
#define CSI_REG_PORT_BASE(p)			\
	({ typeof(p) __p = (p);			\
		__p > 0 ? (0x6c000 + 0x100 * (__p - 1)) : 0x64300; })

/* rx registers, offsets to the port base */
#define CSI2_REG_CSI_RX_ENABLE				0x00
#define CSI2_CSI_RX_ENABLE_ENABLE			0x01
/* Enabled lanes - 1 */
#define CSI2_REG_CSI_RX_NOF_ENABLED_LANES		0x04
#define CSI2_REG_CSI_RX_CONFIG				0x08
#define CSI2_CSI_RX_CONFIG_RELEASE_LP11			0x1
#define CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING	0x2
#define CSI2_CSI_RX_CONFIG_SKEWCAL_ENABLE		0x4
#define CSI2_REG_CSI_RX_HBP_TESTMODE_ENABLE		0x0c
#define CSI2_REG_CSI_RX_ERROR_HANDLING			0x10
#define CSI2_REG_CSI_RX_SYNC_COUNTER_SEL		0x14
#define CSI2_RX_SYNC_COUNTER_INTERNAL			0
#define CSI2_RX_SYNC_COUNTER_EXTERNAL			3
#define CSI2_REG_CSI_RX_SP_IF_CONFIG			0x18
#define CSI2_REG_CSI_RX_LP_IF_CONFIG			0x1C
#define CSI2_REG_CSI_RX_STATUS				0x20
#define CSI2_CSI_RX_STATUS_BUSY				0x01
#define CSI2_REG_CSI_RX_STATUS_DLANE_HS			0x24
#define CSI2_REG_CSI_RX_STATUS_DLANE_LP			0x28
#define CSI2_REG_CSI_RX_DLY_CNT_TERMEN_CLANE		0x2c
#define CSI2_REG_CSI_RX_DLY_CNT_SETTLE_CLANE		0x30
/* 0..3 */
#define CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(n)		(0x34 + (n) * 8)
#define CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(n)		(0x38 + (n) * 8)
#define CSI2_REG_CSI_RX_DLY_CNT_NARROW_SHIFT		4

/* rx error bits in the per-port irq ctrl0 block (low 16 bits) */
#define CSI2_CSIRX_HEADER_SINGLE_ERROR_CORRECTED	BIT(0)
#define CSI2_CSIRX_HEADER_MULTIPLE_ERRORS_CORRECTED	BIT(1)
#define CSI2_CSIRX_PAYLOAD_CRC_ERROR			BIT(2)
#define CSI2_CSIRX_FIFO_OVERFLOW			BIT(3)
#define CSI2_CSIRX_RESERVED_SHORT_PACKET_DATA_TYPE	BIT(4)
#define CSI2_CSIRX_RESERVED_LONG_PACKET_DATA_TYPE	BIT(5)
#define CSI2_CSIRX_INCOMPLETE_LONG_PACKET		BIT(6)
#define CSI2_CSIRX_FRAME_SYNC_ERROR			BIT(7)
#define CSI2_CSIRX_LINE_SYNC_ERROR			BIT(8)
#define CSI2_CSIRX_DPHY_RECOVERABLE_SYNC_ERROR		BIT(9)
#define CSI2_CSIRX_DPHY_NONRECOVERABLE_SYNC_ERROR	BIT(10)
#define CSI2_CSIRX_ESCAPE_MODE_ERROR			BIT(11)
#define CSI2_CSIRX_ESCAPE_MODE_TRIGGER_EVENT		BIT(12)
#define CSI2_CSIRX_ESCAPE_MODE_ULTRALOW_POWER_DATA	BIT(13)
#define CSI2_CSIRX_ESCAPE_MODE_ULTRALOW_POWER_EXIT_CLK	BIT(14)
#define CSI2_CSIRX_INTER_FRAME_SHORT_PACKET_DISCARDED	BIT(15)
#define CSI2_CSIRX_INTER_FRAME_LONG_PACKET_DISCARDED	BIT(16)
#define CSI2_CSIRX_NUM_ERRORS				17

/* sof/eof bits per virtual channel in the per-port irq ctrl0 block */
#define CSI2_IRQ_FS_VC(chn)	(0x10000 << ((chn) * 4))
#define CSI2_IRQ_FE_VC(chn)	(0x20000 << ((chn) * 4))
#define CSI2_IRQ_LS_VC(chn)	(0x40000 << ((chn) * 4))
#define CSI2_IRQ_LE_VC(chn)	(0x80000 << ((chn) * 4))

#endif /* IPU4P_PLATFORM_ISYS_CSI2_REG_H */
