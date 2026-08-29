/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013 - 2024 Intel Corporation */

#ifndef IPU4P_BUS_H
#define IPU4P_BUS_H

#include <linux/auxiliary_bus.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct firmware;
struct pci_dev;

struct ipu4p_buttress_ctrl;

struct ipu4p_bus_device {
	struct auxiliary_device auxdev;
	const struct auxiliary_driver *auxdrv;
	const struct ipu4p_auxdrv_data *auxdrv_data;
	struct list_head list;
	void *pdata;
	struct ipu4p_mmu *mmu;
	struct ipu4p_device *isp;
	const struct ipu4p_buttress_ctrl *ctrl;
	const struct firmware *fw;
	struct sg_table fw_sgt;
	u64 *pkg_dir;
	dma_addr_t pkg_dir_dma_addr;
	unsigned int pkg_dir_size;
};

struct ipu4p_auxdrv_data {
	irqreturn_t (*isr)(struct ipu4p_bus_device *adev);
	irqreturn_t (*isr_threaded)(struct ipu4p_bus_device *adev);
	bool wake_isr_thread;
};

#define to_ipu4p_bus_device(_dev) \
	container_of(to_auxiliary_dev(_dev), struct ipu4p_bus_device, auxdev)
#define auxdev_to_adev(_auxdev) \
	container_of(_auxdev, struct ipu4p_bus_device, auxdev)
#define ipu4p_bus_get_drvdata(adev) dev_get_drvdata(&(adev)->auxdev.dev)

struct ipu4p_bus_device *
ipu4p_bus_initialize_device(struct pci_dev *pdev, struct device *parent,
			   void *pdata, const struct ipu4p_buttress_ctrl *ctrl,
			   char *name);
int ipu4p_bus_add_device(struct ipu4p_bus_device *adev);
void ipu4p_bus_del_devices(struct pci_dev *pdev);

#endif
