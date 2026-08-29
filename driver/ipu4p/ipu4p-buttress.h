/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU4P_BUTTRESS_H
#define IPU4P_BUTTRESS_H

#include <linux/completion.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/mutex.h>

struct device;
struct firmware;
struct ipu4p_device;
struct ipu4p_bus_device;

#define BUTTRESS_PS_FREQ_STEP		25U
#define BUTTRESS_MIN_FORCE_PS_FREQ	(BUTTRESS_PS_FREQ_STEP * 8)
#define BUTTRESS_MAX_FORCE_PS_FREQ	(BUTTRESS_PS_FREQ_STEP * 32)

#define BUTTRESS_IS_FREQ_STEP		25U
#define BUTTRESS_MIN_FORCE_IS_FREQ	(BUTTRESS_IS_FREQ_STEP * 8)
#define BUTTRESS_MAX_FORCE_IS_FREQ	(BUTTRESS_IS_FREQ_STEP * 22)

struct ipu4p_buttress_ctrl {
	u32 freq_ctl, pwr_sts_shift, pwr_sts_mask, pwr_sts_on, pwr_sts_off;
	unsigned int ratio;
	unsigned int ratio_shift;
	unsigned int qos_floor;
	unsigned int ovrd;
	unsigned int ovrd_shift;
};

struct ipu4p_buttress_ipc {
	struct completion send_complete;
	struct completion recv_complete;
	u32 nack;
	u32 nack_mask;
	u32 recv_data;
	u32 csr_out;
	u32 csr_in;
	u32 db0_in;
	u32 db0_out;
	u32 data0_out;
	u32 data0_in;
};

struct ipu4p_buttress {
	struct mutex power_mutex, auth_mutex, cons_mutex, ipc_mutex;
	struct ipu4p_buttress_ipc cse;
	struct list_head constraints;
	u32 wdt_cached_value;
	bool force_suspend;
	u32 ref_clk;
};

struct ipu4p_ipc_buttress_bulk_msg {
	u32 cmd;
	u32 expected_resp;
	bool require_resp;
	u8 cmd_size;
};

int ipu4p_buttress_ipc_reset(struct ipu4p_device *isp,
			    struct ipu4p_buttress_ipc *ipc);
int ipu4p_buttress_map_fw_image(struct ipu4p_bus_device *sys,
			       const struct firmware *fw,
			       struct sg_table *sgt);
void ipu4p_buttress_unmap_fw_image(struct ipu4p_bus_device *sys,
				  struct sg_table *sgt);
int ipu4p_buttress_power(struct device *dev,
			const struct ipu4p_buttress_ctrl *ctrl, bool on);
bool ipu4p_buttress_get_secure_mode(struct ipu4p_device *isp);
int ipu4p_buttress_authenticate(struct ipu4p_device *isp);
int ipu4p_buttress_reset_authentication(struct ipu4p_device *isp);
bool ipu4p_buttress_auth_done(struct ipu4p_device *isp);
int ipu4p_buttress_start_tsc_sync(struct ipu4p_device *isp);
void ipu4p_buttress_tsc_read(struct ipu4p_device *isp, u64 *val);
u64 ipu4p_buttress_tsc_ticks_to_ns(u64 ticks, const struct ipu4p_device *isp);

irqreturn_t ipu4p_buttress_isr(int irq, void *isp_ptr);
irqreturn_t ipu4p_buttress_isr_threaded(int irq, void *isp_ptr);
int ipu4p_buttress_init(struct ipu4p_device *isp);
void ipu4p_buttress_exit(struct ipu4p_device *isp);
void ipu4p_buttress_csi_port_config(struct ipu4p_device *isp,
				   u32 legacy, u32 combo);
void ipu4p_buttress_restore(struct ipu4p_device *isp);
#endif /* IPU4P_BUTTRESS_H */
