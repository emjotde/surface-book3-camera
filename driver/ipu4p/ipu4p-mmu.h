/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2024 Intel Corporation */

#ifndef IPU4P_MMU_H
#define IPU4P_MMU_H

#define ISYS_MMID 1
#define PSYS_MMID 0

#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

struct device;
struct page;
struct ipu4p_hw_variants;

struct ipu4p_mmu_info {
	struct device *dev;

	u32 *l1_pt;
	u32 l1_pt_dma;
	u32 **l2_pts;

	u32 *dummy_l2_pt;
	u32 dummy_l2_pteval;
	void *dummy_page;
	u32 dummy_page_pteval;

	dma_addr_t aperture_start;
	dma_addr_t aperture_end;
	unsigned long pgsize_bitmap;

	spinlock_t lock;	/* Serialize access to users */
	struct ipu4p_dma_mapping *dmap;
};

struct ipu4p_mmu {
	struct list_head node;

	struct ipu4p_mmu_hw *mmu_hw;
	unsigned int nr_mmus;
	unsigned int mmid;

	phys_addr_t pgtbl;
	struct device *dev;

	struct ipu4p_dma_mapping *dmap;
	struct list_head vma_list;

	struct page *trash_page;
	dma_addr_t pci_trash_page; /* IOVA from PCI DMA services (parent) */
	dma_addr_t iova_trash_page; /* IOVA for IPU4P child nodes to use */

	bool ready;
	spinlock_t ready_lock;	/* Serialize access to bool ready */

	void (*tlb_invalidate)(struct ipu4p_mmu *mmu);
};

struct ipu4p_mmu *ipu4p_mmu_init(struct device *dev,
			       void __iomem *base, int mmid,
			       const struct ipu4p_hw_variants *hw);
void ipu4p_mmu_cleanup(struct ipu4p_mmu *mmu);
int ipu4p_mmu_hw_init(struct ipu4p_mmu *mmu);
void ipu4p_mmu_hw_cleanup(struct ipu4p_mmu *mmu);
int ipu4p_mmu_map(struct ipu4p_mmu_info *mmu_info, unsigned long iova,
		 phys_addr_t paddr, size_t size);
void ipu4p_mmu_unmap(struct ipu4p_mmu_info *mmu_info, unsigned long iova,
		    size_t size);
phys_addr_t ipu4p_mmu_iova_to_phys(struct ipu4p_mmu_info *mmu_info,
				  dma_addr_t iova);
#endif
