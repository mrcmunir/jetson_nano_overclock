/*
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_MISC_TEGRA_FUSE_H
#define __DRIVERS_MISC_TEGRA_FUSE_H

#include <linux/dmaengine.h>
#include <linux/types.h>

struct tegra_fuse;

struct tegra_fuse_info {
	u32 (*read)(struct tegra_fuse *fuse, unsigned int offset);
	int (*write)(struct tegra_fuse *fuse, u32 value, unsigned int offset);
	unsigned int size;
	unsigned int spare;
};

struct tegra_fuse_soc {
	void (*init)(struct tegra_fuse *fuse);
	void (*speedo_init)(struct tegra_sku_info *info);
	int (*probe)(struct tegra_fuse *fuse);

	const struct tegra_fuse_info *info;
};

struct tegra_fuse {
	struct device *dev;
	void __iomem *base;
	phys_addr_t phys;
	struct clk *clk;

	u32 (*read_early)(struct tegra_fuse *fuse, unsigned int offset);
	u32 (*read)(struct tegra_fuse *fuse, unsigned int offset);
	int (*write)(struct tegra_fuse *fuse, u32 value, unsigned int offset);
	u32 (*control_read)(struct tegra_fuse *fuse, unsigned int offset);
	int (*control_write)(struct tegra_fuse *fuse, u32 value,
			unsigned int offset);
	const struct tegra_fuse_soc *soc;

	/* APBDMA on Tegra20 */
	struct {
		struct mutex lock;
		struct completion wait;
		struct dma_chan *chan;
		struct dma_slave_config config;
		dma_addr_t phys;
		u32 *virt;
	} apbdma;
};

void tegra_init_revision(void);
void tegra_init_apbmisc(void);

bool tegra_fuse_read_spare(unsigned int spare);
u32 tegra_fuse_read_early(unsigned int offset);
int tegra_fuse_control_read(unsigned long offset, u32 *value);
void tegra_fuse_control_write(u32 value, unsigned long offset);

void tegra20_init_speedo_data(struct tegra_sku_info *sku_info);
void tegra30_init_speedo_data(struct tegra_sku_info *sku_info);
void tegra114_init_speedo_data(struct tegra_sku_info *sku_info);
void tegra124_init_speedo_data(struct tegra_sku_info *sku_info);
void tegra210_init_speedo_data(struct tegra_sku_info *sku_info);

extern const struct tegra_fuse_soc tegra20_fuse_soc;
extern const struct tegra_fuse_soc tegra30_fuse_soc;
extern const struct tegra_fuse_soc tegra114_fuse_soc;
extern const struct tegra_fuse_soc tegra124_fuse_soc;
extern const struct tegra_fuse_soc tegra210_fuse_soc;
extern const struct tegra_fuse_soc tegra186_fuse_soc;
extern const struct tegra_fuse_soc tegra194_fuse_soc;

#endif
