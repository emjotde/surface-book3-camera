// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/sprintf.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "ipu4p-bus.h"
#include "ipu4p-isys.h"
#include "ipu4p-isys-csi2.h"
#include "ipu4p-isys-subdev.h"
#include "ipu4p-platform-isys-csi2-reg.h"
#include "ipu4p-platform-regs.h"

/*
 * Bring-up sniffer: enable the rx on every csi2 port and log the lane
 * state while the sensor streams. This finds the physical port.
 */
static bool csi_sniff;
module_param(csi_sniff, bool, 0444);
MODULE_PARM_DESC(csi_sniff, "Listen on all csi2 ports and log lane state (bring-up)");

static int csettle_ovr = -1;
module_param(csettle_ovr, int, 0444);
MODULE_PARM_DESC(csettle_ovr, "Override clock lane settle count (bring-up)");
static int dsettle_ovr = -1;
module_param(dsettle_ovr, int, 0444);
MODULE_PARM_DESC(dsettle_ovr, "Override data lane settle count (bring-up)");
static int ctermen_ovr = -1;
module_param(ctermen_ovr, int, 0444);
MODULE_PARM_DESC(ctermen_ovr, "Override clock lane termen count (bring-up)");
static int dtermen_ovr = -1;
module_param(dtermen_ovr, int, 0444);
MODULE_PARM_DESC(dtermen_ovr, "Override data lane termen count (bring-up)");

/*
 * Bring-up TPG: use the isys MIPI packet generator as the frame
 * source instead of the sensor. -1 = off, 0 = mipigen0 (s0 cluster),
 * 1 = mipigen1 (s1 cluster). Proves the fw/isys/dma path without
 * a working PHY.
 */
static int tpg_mode = -1;
module_param(tpg_mode, int, 0444);
MODULE_PARM_DESC(tpg_mode, "MIPI pkt gen as source: -1 off, 0 tpg0, 1 tpg1");

static bool tpg_keep_port_source;
module_param(tpg_keep_port_source, bool, 0444);
MODULE_PARM_DESC(tpg_keep_port_source,
		 "Keep the csi2 port fw source while the tpg runs");

#define IPU4P_TPG0_OFFSET	0x66c00
#define IPU4P_TPG1_OFFSET	0x6ec00
#define IPU4P_TPG0_SEL		(0x66800 + 0x1c)
#define IPU4P_TPG1_SEL		(0x6e800 + 0x1c)

#define MIPI_GEN_REG_COM_ENABLE		0x0
#define MIPI_GEN_REG_COM_DTYPE		0x4
#define MIPI_GEN_REG_COM_VTYPE		0x8
#define MIPI_GEN_REG_COM_VCHAN		0xc
#define MIPI_GEN_REG_COM_WCOUNT		0x10
#define MIPI_GEN_REG_SYNG_NOF_FRAMES	0x24
#define MIPI_GEN_REG_SYNG_NOF_PIXELS	0x28
#define MIPI_GEN_REG_SYNG_NOF_LINES	0x2c
#define MIPI_GEN_REG_SYNG_HBLANK_CYC	0x30
#define MIPI_GEN_REG_SYNG_VBLANK_CYC	0x34
#define MIPI_GEN_REG_SYNG_STAT_HCNT	0x38
#define MIPI_GEN_REG_SYNG_STAT_VCNT	0x3c
#define MIPI_GEN_REG_SYNG_STAT_FCNT	0x40
#define MIPI_GEN_REG_SYNG_STAT_DONE	0x44
#define MIPI_GEN_REG_TPG_MODE		0x48
#define MIPI_GEN_REG_TPG_HCNT_MASK	0x4c
#define MIPI_GEN_REG_TPG_VCNT_MASK	0x50
#define MIPI_GEN_REG_TPG_XYCNT_MASK	0x54
#define MIPI_GEN_REG_TPG_HCNT_DELTA	0x58
#define MIPI_GEN_REG_TPG_VCNT_DELTA	0x5c

static void csi2_tpg_stream(struct ipu4p_isys *isys, u32 width, u32 height,
			    u32 bpp, int enable)
{
	void __iomem *isys_base = isys->pdata->base;
	void __iomem *base = isys_base +
		(tpg_mode ? IPU4P_TPG1_OFFSET : IPU4P_TPG0_OFFSET);
	void __iomem *sel = isys_base +
		(tpg_mode ? IPU4P_TPG1_SEL : IPU4P_TPG0_SEL);
	struct device *dev = &isys->adev->auxdev.dev;

	writel(enable ? 1 : 0, sel);

	if (!enable) {
		writel(0, base + MIPI_GEN_REG_COM_ENABLE);
		return;
	}

	writel((bpp - 8) / 2, base + MIPI_GEN_REG_COM_DTYPE);
	writel(0x2b, base + MIPI_GEN_REG_COM_VTYPE);	/* RAW10 */
	writel(0, base + MIPI_GEN_REG_COM_VCHAN);
	writel(0, base + MIPI_GEN_REG_SYNG_NOF_FRAMES);

	writel(DIV_ROUND_UP(width * bpp, 8), base + MIPI_GEN_REG_COM_WCOUNT);
	writel(DIV_ROUND_UP(width, 4), base + MIPI_GEN_REG_SYNG_NOF_PIXELS);
	writel(height, base + MIPI_GEN_REG_SYNG_NOF_LINES);

	writel(1024, base + MIPI_GEN_REG_SYNG_HBLANK_CYC);
	writel(1024, base + MIPI_GEN_REG_SYNG_VBLANK_CYC);

	writel(0, base + MIPI_GEN_REG_TPG_MODE);	/* ramp */
	writel(-1, base + MIPI_GEN_REG_TPG_HCNT_MASK);
	writel(-1, base + MIPI_GEN_REG_TPG_VCNT_MASK);
	writel(-1, base + MIPI_GEN_REG_TPG_XYCNT_MASK);
	writel(0, base + MIPI_GEN_REG_TPG_HCNT_DELTA);
	writel(0, base + MIPI_GEN_REG_TPG_VCNT_DELTA);

	writel(2, base + MIPI_GEN_REG_COM_ENABLE);

	dev_info(dev, "tpg%d on: %ux%u bpp %u\n", tpg_mode, width, height, bpp);
}

static struct ipu4p_isys *sniff_isys;
static atomic_t sniff_rounds = ATOMIC_INIT(0);
static void csi2_sniff_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(sniff_work, csi2_sniff_work_fn);

static void csi2_sniff_dump(struct ipu4p_isys *isys, const char *tag)
{
	const struct ipu4p_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	unsigned int i;

	{
		void __iomem *isp_base = isys->adev->isp->base;
		unsigned int g;

		for (g = 0; g < 8; g++) {
			dev_info(dev,
				 "sniff[%s] phy g%u: cphy_dll %08x cphy_rx1 %08x dphy_dll %08x dphy_rx %08x afe %08x\n",
				 tag, g,
				 readl(isp_base + 0x10100 + g * 0x100),
				 readl(isp_base + 0x10110 + g * 0x100),
				 readl(isp_base + 0x1014c + g * 0x100),
				 readl(isp_base + 0x10158 + g * 0x100),
				 readl(isp_base + 0x10174 + g * 0x100));
		}
		dev_info(dev, "sniff[%s] btrs: port_cfg_ab %08x bscan %08x\n",
			 tag,
			 readl(isp_base + 0x200),
			 readl(isp_base + 0x100d8));

		/* buttress clock tree: is/ps freq ctl, ljpll, sensor clk ctl */
		dev_info(dev,
			 "sniff[%s] btrs clk: is_freq %08x ps_freq %08x sensor_freq(ljpll) %08x sensor_clk %08x\n",
			 tag,
			 readl(isp_base + 0x34),
			 readl(isp_base + 0x38),
			 readl(isp_base + 0x16c),
			 readl(isp_base + 0x170));

		/* FW liveness: syscom state, buttress isr, spc status */
		if (isys->fwcom)
			dev_info(dev,
				 "sniff[%s] fw: syscom_state %08x btrs_isr %08x fwcom_rdy %d\n",
				 tag,
				 readl(isys->pdata->base +
				       isys->pdata->ipdata->hw_variant.dmem_offset +
				       2 * 4),
				 readl(isp_base + 0x90),
				 ipu4p_fw_com_ready(isys->fwcom));

		/* GP banks: srst, srst_slv, hpll_freq, isclk_ratio, ovr, portcfg */
		for (g = 0; g < 2; g++) {
			void __iomem *gp = isys_base + (g ? 0x6e800 : 0x66800);

			dev_info(dev,
				 "sniff[%s] gp%u: srst %x slv %x hpll %x ratio %x ovr %x cfg %x rcompd %x rcompv %x\n",
				 tag, g,
				 readl(gp + 0x00), readl(gp + 0x04),
				 readl(gp + 0x08), readl(gp + 0x0c),
				 readl(gp + 0x10), readl(gp + 0x14),
				 readl(gp + 0x18), readl(gp + 0x1c));
		}

		/* PMC isCLK (imgclk) block, ICLK opregion in the DSDT */
		{
			void __iomem *iclk = ioremap(0xfdad8000, 0x40);

			if (iclk) {
				dev_info(dev,
					 "sniff[%s] isclk: %02x %02x %02x %02x %02x %02x\n",
					 tag,
					 readb(iclk + 0x00), readb(iclk + 0x0c),
					 readb(iclk + 0x18), readb(iclk + 0x24),
					 readb(iclk + 0x30), readb(iclk + 0x3c));
				iounmap(iclk);
			}
		}
	}

	if (tpg_mode >= 0) {
		void __iomem *tbase = isys_base +
			(tpg_mode ? IPU4P_TPG1_OFFSET : IPU4P_TPG0_OFFSET);

		dev_info(dev,
			 "sniff[%s] tpg%d: en %x hcnt %x vcnt %x fcnt %x done %x\n",
			 tag, tpg_mode,
			 readl(tbase + MIPI_GEN_REG_COM_ENABLE),
			 readl(tbase + MIPI_GEN_REG_SYNG_STAT_HCNT),
			 readl(tbase + MIPI_GEN_REG_SYNG_STAT_VCNT),
			 readl(tbase + MIPI_GEN_REG_SYNG_STAT_FCNT),
			 readl(tbase + MIPI_GEN_REG_SYNG_STAT_DONE));
	}

	for (i = 0; i < csi2_pdata->nports; i++) {
		void __iomem *base = isys->csi2[i].base;

		if (!base)
			continue;

		dev_info(dev,
			 "sniff[%s] csi2-%u: en %x nl %x cfg %x st %x hs %x lp %x irq0 %08x irq %x\n",
			 tag, i,
			 readl(base + CSI2_REG_CSI_RX_ENABLE),
			 readl(base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES),
			 readl(base + CSI2_REG_CSI_RX_CONFIG),
			 readl(base + CSI2_REG_CSI_RX_STATUS),
			 readl(base + CSI2_REG_CSI_RX_STATUS_DLANE_HS),
			 readl(base + CSI2_REG_CSI_RX_STATUS_DLANE_LP),
			 readl(isys_base +
			       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i) + 0x8),
			 readl(isys_base +
			       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i) + 0x8));
	}
}

static void csi2_sniff_work_fn(struct work_struct *work)
{
	struct ipu4p_isys *isys = READ_ONCE(sniff_isys);

	if (!isys)
		return;

	csi2_sniff_dump(isys, "live");

	if (atomic_dec_return(&sniff_rounds) > 0)
		schedule_delayed_work(&sniff_work, msecs_to_jiffies(700));
}

/* arm the rx and the ctrl0 latches on every inactive port */
static void csi2_sniff_arm(struct ipu4p_isys *isys, unsigned int active_port,
			   const struct ipu4p_isys_csi2_timing *timing)
{
	const struct ipu4p_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	void __iomem *isys_base = isys->pdata->base;
	u32 csi2part = 0xffff;
	unsigned int i, j;
	u32 val;

	for (j = 0; j < NR_OF_CSI2_VC; j++)
		csi2part |= CSI2_IRQ_FS_VC(j) | CSI2_IRQ_FE_VC(j);

	for (i = 0; i < csi2_pdata->nports; i++) {
		void __iomem *base = isys->csi2[i].base;

		if (i == active_port || !base)
			continue;

		writel(timing->ctermen,
		       base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_CLANE);
		writel(timing->csettle,
		       base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_CLANE);
		for (j = 0; j < 4; j++) {
			writel(timing->dtermen,
			       base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(j));
			writel(timing->dsettle,
			       base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(j));
		}

		val = readl(base + CSI2_REG_CSI_RX_CONFIG);
		val |= CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
			CSI2_CSI_RX_CONFIG_RELEASE_LP11;
		writel(val, base + CSI2_REG_CSI_RX_CONFIG);

		/* arm the lane state trackers (cio2 heritage) */
		writel(0xff, base + CSI2_REG_CSI_RX_STATUS_DLANE_HS);
		writel(0xffffff, base + CSI2_REG_CSI_RX_STATUS_DLANE_LP);

		writel(1, base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES);
		writel(CSI2_CSI_RX_ENABLE_ENABLE, base + CSI2_REG_CSI_RX_ENABLE);

		/* latch sync and error bits; do not route the irq */
		writel(1, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i));
		writel(0, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i) + 0x14);
		writel(0xffffffff,
		       isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i) + 0xc);
		writel(1, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i) + 0x4);
		writel(0, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(i) + 0x10);

		writel(csi2part, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i));
		writel(0, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i) + 0x14);
		writel(0xffffffff,
		       isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i) + 0xc);
		writel(csi2part,
		       isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i) + 0x4);
		writel(0, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(i) + 0x10);
	}
}

/* stop the sniffer and disable the rx on every inactive port */
static void csi2_sniff_disarm(struct ipu4p_isys *isys, unsigned int active_port)
{
	const struct ipu4p_isys_internal_csi2_pdata *csi2_pdata =
		&isys->pdata->ipdata->csi2;
	unsigned int i;
	u32 val;

	WRITE_ONCE(sniff_isys, NULL);
	cancel_delayed_work_sync(&sniff_work);

	csi2_sniff_dump(isys, "off");

	for (i = 0; i < csi2_pdata->nports; i++) {
		void __iomem *base = isys->csi2[i].base;

		if (i == active_port || !base)
			continue;

		val = readl(base + CSI2_REG_CSI_RX_CONFIG);
		val &= ~(CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
			 CSI2_CSI_RX_CONFIG_RELEASE_LP11);
		writel(val, base + CSI2_REG_CSI_RX_CONFIG);
		writel(0, base + CSI2_REG_CSI_RX_ENABLE);
	}
}

static const u32 csi2_supported_codes[] = {
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_META_8,
	MEDIA_BUS_FMT_META_10,
	MEDIA_BUS_FMT_META_12,
	MEDIA_BUS_FMT_META_16,
	MEDIA_BUS_FMT_META_24,
	0
};

/*
 * Strings corresponding to CSI-2 receiver errors are here.
 * Corresponding macros are defined in the header file.
 */
/* IPU4 csi2 receiver error bits, low 17 bits of the ctrl0 status */
static const struct ipu4p_csi2_error dphy_rx_errors[] = {
	{ "Single packet header error corrected", true },
	{ "Multiple packet header errors detected", true },
	{ "Payload checksum (CRC) error", true },
	{ "FIFO overflow", false },
	{ "Reserved short packet data type detected", true },
	{ "Reserved long packet data type detected", true },
	{ "Incomplete long packet detected", false },
	{ "Frame sync error", false },
	{ "Line sync error", false },
	{ "DPHY recoverable synchronization error", true },
	{ "DPHY non-recoverable synchronization error", false },
	{ "Escape mode error", true },
	{ "Escape mode trigger event", true },
	{ "Escape mode ultra-low power state for data lane(s)", true },
	{ "Escape mode ultra-low power state exit for clock lane", true },
	{ "Inter-frame short packet discarded", true },
	{ "Inter-frame long packet discarded", true },
};

s64 ipu4p_isys_csi2_get_link_freq(struct ipu4p_isys_csi2 *csi2)
{
	struct media_pad *src_pad;

	if (!csi2)
		return -EINVAL;

	src_pad = media_entity_remote_source_pad_unique(&csi2->asd.sd.entity);
	if (IS_ERR(src_pad)) {
		dev_err(&csi2->isys->adev->auxdev.dev,
			"can't get source pad of %s (%ld)\n",
			csi2->asd.sd.name, PTR_ERR(src_pad));
		return PTR_ERR(src_pad);
	}

	return v4l2_get_link_freq(src_pad, 0, 0);
}

static int csi2_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	struct ipu4p_isys_subdev *asd = to_ipu4p_isys_subdev(sd);
	struct ipu4p_isys_csi2 *csi2 = to_ipu4p_isys_csi2(asd);
	struct device *dev = &csi2->isys->adev->auxdev.dev;

	dev_dbg(dev, "csi2 subscribe event(type %u id %u)\n",
		sub->type, sub->id);

	switch (sub->type) {
	case V4L2_EVENT_FRAME_SYNC:
		return v4l2_event_subscribe(fh, sub, 10, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops csi2_sd_core_ops = {
	.subscribe_event = csi2_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

/*
 * The input system CSI2+ receiver has several
 * parameters affecting the receiver timings. These depend
 * on the MIPI bus frequency F in Hz (sensor transmitter rate)
 * as follows:
 *	register value = (A/1e9 + B * UI) / COUNT_ACC
 * where
 *	UI = 1 / (2 * F) in seconds
 *	COUNT_ACC = counter accuracy in seconds
 *	COUNT_ACC = 0.125 ns = 1 / 8 ns, ACCINV = 8.
 *
 * A and B are coefficients from the table below,
 * depending whether the register minimum or maximum value is
 * calculated.
 *				       Minimum     Maximum
 * Clock lane			       A     B     A     B
 * reg_rx_csi_dly_cnt_termen_clane     0     0    38     0
 * reg_rx_csi_dly_cnt_settle_clane    95    -8   300   -16
 * Data lanes
 * reg_rx_csi_dly_cnt_termen_dlane0    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane0   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane1    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane1   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane2    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane2   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane3    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane3   85    -2   145    -6
 *
 * We use the minimum values of both A and B.
 */

#define DIV_SHIFT	8
#define CSI2_ACCINV	8

static u32 calc_timing(s32 a, s32 b, s64 link_freq, s32 accinv)
{
	return accinv * a + (accinv * b * (500000000 >> DIV_SHIFT)
			     / (s32)(link_freq >> DIV_SHIFT));
}

static int
ipu4p_isys_csi2_calc_timing(struct ipu4p_isys_csi2 *csi2,
			   struct ipu4p_isys_csi2_timing *timing, s32 accinv)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	s64 link_freq;

	link_freq = ipu4p_isys_csi2_get_link_freq(csi2);
	if (link_freq < 0)
		return link_freq;

	timing->ctermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_B,
				      link_freq, accinv);
	timing->csettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_B,
				      link_freq, accinv);
	timing->dtermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_B,
				      link_freq, accinv);
	timing->dsettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_B,
				      link_freq, accinv);

	if (csettle_ovr >= 0)
		timing->csettle = csettle_ovr;
	if (dsettle_ovr >= 0)
		timing->dsettle = dsettle_ovr;
	if (ctermen_ovr >= 0)
		timing->ctermen = ctermen_ovr;
	if (dtermen_ovr >= 0)
		timing->dtermen = dtermen_ovr;

	dev_dbg(dev, "ctermen %u csettle %u dtermen %u dsettle %u\n",
		timing->ctermen, timing->csettle,
		timing->dtermen, timing->dsettle);

	return 0;
}

void ipu4p_isys_register_errors(struct ipu4p_isys_csi2 *csi2)
{
	struct ipu4p_isys *isys = csi2->isys;
	void __iomem *isys_base = isys->pdata->base;
	u32 status;

	status = readl(isys_base +
		       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x8);
	writel(status, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0xc);

	csi2->receiver_errors |= status & 0xffff;
}

void ipu4p_isys_csi2_error(struct ipu4p_isys_csi2 *csi2)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	const struct ipu4p_csi2_error *errors;
	u32 status;
	u32 i;

	/* register errors once more in case of interrupts are disabled */
	ipu4p_isys_register_errors(csi2);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;
	errors = dphy_rx_errors;

	for (i = 0; i < ARRAY_SIZE(dphy_rx_errors); i++) {
		if (!(status & BIT(i)))
			continue;
		if (errors[i].is_info_only)
			dev_dbg(dev, "csi2-%i info: %s\n",
				csi2->port, errors[i].error_string);
		else
			dev_dbg(dev, "csi2-%i error: %s\n",
					    csi2->port, errors[i].error_string);
	}
}

static int ipu4p_isys_csi2_set_stream(struct v4l2_subdev *sd,
				     const struct ipu4p_isys_csi2_timing *timing,
				     unsigned int nlanes, int enable)
{
	struct ipu4p_isys_subdev *asd = to_ipu4p_isys_subdev(sd);
	struct ipu4p_isys_csi2 *csi2 = to_ipu4p_isys_csi2(asd);
	struct ipu4p_isys *isys = csi2->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	u32 csi2part = 0;
	int ret = 0;
	u32 val;
	u32 i;

	dev_dbg(dev, "stream %s CSI2-%u with %u lanes\n", enable ? "on" : "off",
		csi2->port, nlanes);

	if (!enable) {
		if (tpg_mode >= 0)
			csi2_tpg_stream(isys, 0, 0, 10, 0);
		if (csi_sniff)
			csi2_sniff_disarm(isys, csi2->port);

		/* diagnostic: lane states from the finished session */
		dev_dbg(dev,
			"csi2-%u diag: rx_en %x nlanes %x cfg %x status %x hs %x lp %x sip1 %x\n",
			 csi2->port,
			 readl(csi2->base + CSI2_REG_CSI_RX_ENABLE),
			 readl(csi2->base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES),
			 readl(csi2->base + CSI2_REG_CSI_RX_CONFIG),
			 readl(csi2->base + CSI2_REG_CSI_RX_STATUS),
			 readl(csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_HS),
			 readl(csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_LP),
			 readl(isys_base + IPU4P_REG_ISYS_SIP1_IRQ_CTRL_STATUS));

		ipu4p_isys_csi2_error(csi2);

		val = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);
		val &= ~(CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
			 CSI2_CSI_RX_CONFIG_RELEASE_LP11);
		writel(val, csi2->base + CSI2_REG_CSI_RX_CONFIG);

		writel(0, csi2->base + CSI2_REG_CSI_RX_ENABLE);

		/* disable the per-port irq ctrl blocks */
		writel(0, isys_base +
		       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0x4);
		writel(0, isys_base +
		       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0x10);
		writel(0, isys_base +
		       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x4);
		writel(0, isys_base +
		       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x10);
		return 0;
	}

	/* arm the lane state trackers (cio2 heritage) */
	writel(0xff, csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_HS);
	writel(0xffffff, csi2->base + CSI2_REG_CSI_RX_STATUS_DLANE_LP);

	/* D-PHY timing: clock lane, then each enabled data lane */
	writel(timing->ctermen,
	       csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_CLANE);
	writel(timing->csettle,
	       csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_CLANE);

	for (i = 0; i < nlanes; i++) {
		writel(timing->dtermen,
		       csi2->base + CSI2_REG_CSI_RX_DLY_CNT_TERMEN_DLANE(i));
		writel(timing->dsettle,
		       csi2->base + CSI2_REG_CSI_RX_DLY_CNT_SETTLE_DLANE(i));
	}

	val = readl(csi2->base + CSI2_REG_CSI_RX_CONFIG);
	val |= CSI2_CSI_RX_CONFIG_DISABLE_BYTE_CLK_GATING |
		CSI2_CSI_RX_CONFIG_RELEASE_LP11;
	writel(val, csi2->base + CSI2_REG_CSI_RX_CONFIG);

	writel(nlanes, csi2->base + CSI2_REG_CSI_RX_NOF_ENABLED_LANES);
	writel(CSI2_CSI_RX_ENABLE_ENABLE, csi2->base + CSI2_REG_CSI_RX_ENABLE);

	/* sof/eof for all four virtual channels */
	for (i = 0; i < NR_OF_CSI2_VC; i++)
		csi2part |= CSI2_IRQ_FS_VC(i) | CSI2_IRQ_FE_VC(i);

	/* enable csi2 receiver error interrupts (ctrl block) */
	writel(1, isys_base + IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port));
	writel(0, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0x14);
	writel(0xffffffff, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0xc);
	writel(1, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0x4);
	writel(1, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL_BASE(csi2->port) + 0x10);

	/* sync + error bits in the ctrl0 block */
	csi2part |= 0xffff;
	writel(csi2part, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port));
	writel(0, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x14);
	writel(0xffffffff, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0xc);
	writel(csi2part, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x4);
	writel(csi2part, isys_base +
	       IPU4P_REG_ISYS_CSI_IRQ_CTRL0_BASE(csi2->port) + 0x10);

	if (csi_sniff) {
		csi2_sniff_arm(isys, csi2->port, timing);
		WRITE_ONCE(sniff_isys, isys);
		atomic_set(&sniff_rounds, 6);
		schedule_delayed_work(&sniff_work, msecs_to_jiffies(600));
	}

	if (tpg_mode >= 0)
		csi2_tpg_stream(isys, 1280, 800, 10, 1);

	return ret;
}

static int ipu4p_isys_csi2_enable_streams(struct v4l2_subdev *sd,
					 struct v4l2_subdev_state *state,
					 u32 pad, u64 streams_mask)
{
	struct ipu4p_isys_subdev *asd = to_ipu4p_isys_subdev(sd);
	struct ipu4p_isys_csi2 *csi2 = to_ipu4p_isys_csi2(asd);
	struct ipu4p_isys_csi2_timing timing = { };
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 sink_streams;
	int ret;

	remote_pad = media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);

	ret = ipu4p_isys_csi2_calc_timing(csi2, &timing, CSI2_ACCINV);
	if (ret)
		return ret;

	ret = ipu4p_isys_csi2_set_stream(sd, &timing, csi2->nlanes, true);
	if (ret)
		return ret;

	ret = v4l2_subdev_enable_streams(remote_sd, remote_pad->index,
					 sink_streams);
	if (ret) {
		ipu4p_isys_csi2_set_stream(sd, NULL, 0, false);
		return ret;
	}

	return 0;
}

static int ipu4p_isys_csi2_disable_streams(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 streams_mask)
{
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 sink_streams;

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);

	remote_pad = media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	ipu4p_isys_csi2_set_stream(sd, NULL, 0, false);

	v4l2_subdev_disable_streams(remote_sd, remote_pad->index, sink_streams);

	return 0;
}

static int ipu4p_isys_csi2_set_sel(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct ipu4p_isys_subdev *asd = to_ipu4p_isys_subdev(sd);
	struct device *dev = &asd->isys->adev->auxdev.dev;
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_mbus_framefmt *src_ffmt;
	struct v4l2_rect *crop;

	if (sel->pad == CSI2_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	src_ffmt = v4l2_subdev_state_get_format(state, sel->pad, sel->stream);
	if (!src_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	/* Only vertical cropping is supported */
	sel->r.left = 0;
	sel->r.width = sink_ffmt->width;
	/* Non-bayer formats can't be single line cropped */
	if (!ipu4p_isys_is_bayer_format(sink_ffmt->code))
		sel->r.top &= ~1;
	sel->r.height = clamp(sel->r.height & ~1, IPU4P_ISYS_MIN_HEIGHT,
			      sink_ffmt->height - sel->r.top);
	*crop = sel->r;

	/* update source pad format */
	src_ffmt->width = sel->r.width;
	src_ffmt->height = sel->r.height;
	if (ipu4p_isys_is_bayer_format(sink_ffmt->code))
		src_ffmt->code = ipu4p_isys_convert_bayer_order(sink_ffmt->code,
							       sel->r.left,
							       sel->r.top);
	dev_dbg(dev, "set crop for %s sel: %d,%d,%d,%d code: 0x%x\n",
		sd->name, sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		src_ffmt->code);

	return 0;
}

static int ipu4p_isys_csi2_get_sel(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_rect *crop;
	int ret = 0;

	if (sd->entity.pads[sel->pad].flags & MEDIA_PAD_FL_SINK)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = sink_ffmt->width;
		sel->r.height = sink_ffmt->height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_subdev_pad_ops csi2_sd_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ipu4p_isys_subdev_set_fmt,
	.get_selection = ipu4p_isys_csi2_get_sel,
	.set_selection = ipu4p_isys_csi2_set_sel,
	.enum_mbus_code = ipu4p_isys_subdev_enum_mbus_code,
	.set_routing = ipu4p_isys_subdev_set_routing,
	.enable_streams = ipu4p_isys_csi2_enable_streams,
	.disable_streams = ipu4p_isys_csi2_disable_streams,
};

static const struct v4l2_subdev_ops csi2_sd_ops = {
	.core = &csi2_sd_core_ops,
	.pad = &csi2_sd_pad_ops,
};

static const struct media_entity_operations csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

void ipu4p_isys_csi2_cleanup(struct ipu4p_isys_csi2 *csi2)
{
	if (!csi2->isys)
		return;

	v4l2_device_unregister_subdev(&csi2->asd.sd);
	v4l2_subdev_cleanup(&csi2->asd.sd);
	ipu4p_isys_subdev_cleanup(&csi2->asd);
	csi2->isys = NULL;
}

int ipu4p_isys_csi2_init(struct ipu4p_isys_csi2 *csi2,
			struct ipu4p_isys *isys,
			void __iomem *base, unsigned int index)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int ret;

	csi2->isys = isys;
	csi2->base = base;
	csi2->port = index;

	csi2->asd.sd.entity.ops = &csi2_entity_ops;
	csi2->asd.isys = isys;
	ret = ipu4p_isys_subdev_init(&csi2->asd, &csi2_sd_ops, 0,
				    NR_OF_CSI2_SINK_PADS, NR_OF_CSI2_SRC_PADS);
	if (ret)
		goto fail;

	/*
	 * 4.19 ipu-isys-csi2.c, CONFIG_VIDEO_INTEL_IPU4P:
	 *   src = index ? (index + 5) : (index + 3);
	 * Index 0 is s0p3 (FW source 3, legacy PORT3). Indexes 1..4 are
	 * s1p0..s1p3 (FW sources 6..9, the 3PH combo receivers).
	 */
	csi2->asd.source = IPU4P_FW_ISYS_STREAM_SRC_CSI2_PORT0 +
		(index ? index + 5 : index + 3);
	if (tpg_mode >= 0 && !tpg_keep_port_source)
		csi2->asd.source = IPU4P_FW_ISYS_STREAM_SRC_MIPIGEN_0 + tpg_mode;
	csi2->asd.supported_codes = csi2_supported_codes;
	snprintf(csi2->asd.sd.name, sizeof(csi2->asd.sd.name),
		 IPU4P_ISYS_ENTITY_PREFIX " CSI2 %u", index);
	v4l2_set_subdevdata(&csi2->asd.sd, &csi2->asd);
	ret = v4l2_subdev_init_finalize(&csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to init v4l2 subdev\n");
		goto fail;
	}

	ret = v4l2_device_register_subdev(&isys->v4l2_dev, &csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to register v4l2 subdev\n");
		goto fail;
	}

	return 0;

fail:
	ipu4p_isys_csi2_cleanup(csi2);

	return ret;
}

void ipu4p_isys_csi2_sof_event_by_stream(struct ipu4p_isys_stream *stream)
{
	struct video_device *vdev = stream->asd->sd.devnode;
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu4p_isys_csi2 *csi2 = ipu4p_isys_subdev_to_csi2(stream->asd);
	struct v4l2_event ev = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	ev.u.frame_sync.frame_sequence = atomic_fetch_inc(&stream->sequence);
	v4l2_event_queue(vdev, &ev);

	dev_dbg(dev, "sof_event::csi2-%i sequence: %i, vc: %d\n",
		csi2->port, ev.u.frame_sync.frame_sequence, stream->vc);
}

void ipu4p_isys_csi2_eof_event_by_stream(struct ipu4p_isys_stream *stream)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu4p_isys_csi2 *csi2 = ipu4p_isys_subdev_to_csi2(stream->asd);
	u32 frame_sequence = atomic_read(&stream->sequence);

	dev_dbg(dev, "eof_event::csi2-%i sequence: %i\n",
		csi2->port, frame_sequence);
}

int ipu4p_isys_csi2_get_remote_desc(u32 source_stream,
				   struct ipu4p_isys_csi2 *csi2,
				   struct media_entity *source_entity,
				   struct v4l2_mbus_frame_desc_entry *entry)
{
	struct v4l2_mbus_frame_desc_entry *desc_entry = NULL;
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	struct v4l2_mbus_frame_desc desc;
	struct v4l2_subdev *source;
	struct media_pad *pad;
	unsigned int i;
	int ret;

	source = media_entity_to_v4l2_subdev(source_entity);
	if (!source)
		return -EPIPE;

	pad = media_pad_remote_pad_first(&csi2->asd.pad[CSI2_PAD_SINK]);
	if (!pad)
		return -EPIPE;

	ret = v4l2_subdev_call(source, pad, get_frame_desc, pad->index, &desc);
	if (ret)
		return ret;

	if (desc.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
		dev_err(dev, "Unsupported frame descriptor type\n");
		return -EINVAL;
	}

	for (i = 0; i < desc.num_entries; i++) {
		if (source_stream == desc.entry[i].stream) {
			desc_entry = &desc.entry[i];
			break;
		}
	}

	if (!desc_entry) {
		dev_err(dev, "Failed to find stream %u from remote subdev\n",
			source_stream);
		return -EINVAL;
	}

	if (desc_entry->bus.csi2.vc >= NR_OF_CSI2_VC) {
		dev_err(dev, "invalid vc %d\n", desc_entry->bus.csi2.vc);
		return -EINVAL;
	}

	*entry = *desc_entry;

	return 0;
}
