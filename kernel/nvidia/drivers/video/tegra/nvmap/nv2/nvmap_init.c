/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/nvmap.h>
#include <linux/tegra-ivc.h>
#include <linux/dma-contiguous.h>
#include <linux/version.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/sched/clock.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#include <linux/cma.h>
#endif

#include <asm/dma-contiguous.h>

#include "iomap.h"
#include "board.h"
#include <linux/platform/tegra/common.h>

#ifdef CONFIG_TEGRA_VIRTUALIZATION
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#include <soc/tegra/virt/syscalls.h>
#else
#include "../../../drivers/virt/tegra/syscalls.h"
#endif
#endif

#include "nvmap_carveout.h"
#include "nvmap_heap.h"
#include "nvmap_dev.h"
#include "nvmap_init.h"

extern ulong nvmap_init_time;

phys_addr_t __weak tegra_carveout_start;
phys_addr_t __weak tegra_carveout_size;

phys_addr_t __weak tegra_vpr_start;
phys_addr_t __weak tegra_vpr_size;
bool __weak tegra_vpr_resize;

struct device __weak tegra_generic_dev;

struct device __weak tegra_vpr_dev;
EXPORT_SYMBOL(tegra_vpr_dev);

struct device __weak tegra_iram_dev;
struct device __weak tegra_generic_cma_dev;
struct device __weak tegra_vpr_cma_dev;
struct dma_resize_notifier_ops __weak vpr_dev_ops;

__weak const struct of_device_id nvmap_of_ids[] = {
	{ .compatible = "nvidia,carveouts" },
	{ .compatible = "nvidia,carveouts-t18x" },
	{ }
};

static struct dma_declare_info generic_dma_info = {
	.name = "generic",
	.size = 0,
	.notifier.ops = NULL,
};

static struct dma_declare_info vpr_dma_info = {
	.name = "vpr",
	.size = SZ_32M,
	.notifier.ops = &vpr_dev_ops,
};

static struct nvmap_platform_carveout nvmap_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_iram_dev,
		.disable_dynamic_dma_map = true,
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_generic_dev,
		.cma_dev	= &tegra_generic_cma_dev,
		.dma_info	= &generic_dma_info,
	},
	[2] = {
		.name		= "vpr",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VPR,
		.base		= 0,
		.size		= 0,
		.dma_dev	= &tegra_vpr_dev,
		.cma_dev	= &tegra_vpr_cma_dev,
		.dma_info	= &vpr_dma_info,
		.enable_static_dma_map = true,
	},
	[3] = {
		.name		= "vidmem",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_VIDMEM,
		.base		= 0,
		.size		= 0,
		.disable_dynamic_dma_map = true,
		.no_cpu_access = true,
	},
	/* Need uninitialized entries for IVM carveouts */
	[4] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
	},
	[5] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
	},
	[6] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
	},
	[7] = {
		.name		= NULL,
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IVM,
	},
};

static struct nvmap_platform_data nvmap_data = {
	.carveouts	= nvmap_carveouts,
	.nr_carveouts	= 4,
};

static struct nvmap_platform_carveout *nvmap_get_carveout_pdata(const char *name)
{
	struct nvmap_platform_carveout *co;
	for (co = nvmap_carveouts;
	     co < nvmap_carveouts + ARRAY_SIZE(nvmap_carveouts); co++) {
		int i = min_t(int, strcspn(name, "_"), strcspn(name, "-"));
		/* handle IVM carveouts */
		if ((co->usage_mask == NVMAP_HEAP_CARVEOUT_IVM) &&  !co->name)
			goto found;

		if (strncmp(co->name, name, i))
			continue;
found:
		co->dma_dev = co->dma_dev ? co->dma_dev : &co->dev;
		return co;
	}
	pr_err("not enough space for all nvmap carveouts\n");
	return NULL;
}

int nvmap_register_vidmem_carveout(struct device *dma_dev,
				phys_addr_t base, size_t size)
{
	struct nvmap_platform_carveout *vidmem_co;

	if (!base || !size || (base != PAGE_ALIGN(base)) ||
	    (size != PAGE_ALIGN(size)))
		return -EINVAL;

	vidmem_co = nvmap_get_carveout_pdata("vidmem");
	if (!vidmem_co)
		return -ENODEV;

	if (vidmem_co->base || vidmem_co->size)
		return -EEXIST;

	vidmem_co->base = base;
	vidmem_co->size = size;
	if (dma_dev)
		vidmem_co->dma_dev = dma_dev;
	return nvmap_carveout_create(vidmem_co);
}
EXPORT_SYMBOL(nvmap_register_vidmem_carveout);

#ifdef CONFIG_TEGRA_VIRTUALIZATION
int __init nvmap_populate_ivm_carveout(struct reserved_mem *rmem)
{
	u32 id;
	struct tegra_hv_ivm_cookie *ivm;
	struct nvmap_platform_carveout *co;
	unsigned int guestid;
	unsigned long fdt_node = rmem->fdt_node;
	const __be32 *prop;
	int len;
	char *name;
	int ret = 0;

	co = nvmap_get_carveout_pdata(rmem->name);
	if (!co)
		return -ENOMEM;

	if (hyp_read_gid(&guestid)) {
		pr_err("failed to read gid\n");
		return -EINVAL;
	}

	prop = of_get_flat_dt_prop(fdt_node, "ivm", &len);
	if (!prop) {
		pr_err("failed to read ivm property\n");
		return -EINVAL;
	}

	id = of_read_number(prop + 1, 1);
	ivm = tegra_hv_mempool_reserve(id);
	if (IS_ERR_OR_NULL(ivm)) {
		pr_err("failed to reserve IVM memory pool %d\n", id);
		return -ENOMEM;
	}

	/* XXX: Are these the available fields from IVM cookie? */
	co->base     = (phys_addr_t)ivm->ipa;
	co->peer     = ivm->peer_vmid;
	co->size     = ivm->size;
	co->vmid     = (int)guestid;

	if (!co->base || !co->size) {
		ret = -EINVAL;
		goto fail;
	}

	/* See if this VM can allocate (or just create handle from ID)
	 * generated by peer partition */
	prop = of_get_flat_dt_prop(fdt_node, "alloc", &len);
	if (!prop) {
		pr_err("failed to read alloc property\n");
		ret = -EINVAL;
		goto fail;
	}

	name = kzalloc(32, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto fail;
	}

	co->can_alloc = of_read_number(prop, 1);

	co->is_ivm    = true;

	sprintf(name, "ivm%02d%02d%02d", co->vmid, co->peer, co->can_alloc);
	pr_info("IVM carveout IPA:%p, size=%zu, peer vmid=%d, name=%s\n",
		(void *)(uintptr_t)co->base, co->size, co->peer, name);

	co->name      = name;
	nvmap_data.nr_carveouts++;

	return 0;

fail:
	co->base     = 0;
	co->peer     = 0;
	co->size     = 0;
	co->vmid     = 0;
	return ret;
}
#else
int __init nvmap_populate_ivm_carveout(struct reserved_mem *rmem)
{
	return -EINVAL;
}
#endif

static int __nvmap_init_legacy(struct device *dev);
static int __nvmap_init_dt(struct platform_device *pdev)
{
	if (!of_match_device(nvmap_of_ids, &pdev->dev)) {
		pr_err("Missing DT entry!\n");
		return -EINVAL;
	}

	/* For VM_2 we need carveout. So, enabling it here */
	__nvmap_init_legacy(&pdev->dev);

	pdev->dev.platform_data = &nvmap_data;

	return 0;
}

static int __init nvmap_co_device_init(struct reserved_mem *rmem,
					struct device *dev)
{
	struct nvmap_platform_carveout *co = rmem->priv;
	int err;

	if (!co)
		return -ENODEV;

	if (co->usage_mask == NVMAP_HEAP_CARVEOUT_IVM)
		return nvmap_populate_ivm_carveout(rmem);

	/* if co size is 0, => co is not present. So, skip init. */
	if (!co->size)
		return 0;

	if (!co->cma_dev) {
		err = dma_declare_coherent_memory(co->dma_dev, 0,
				co->base, co->size,
				DMA_MEMORY_NOMAP | DMA_MEMORY_EXCLUSIVE);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
		if (!err) {
#else
		if (err & DMA_MEMORY_NOMAP) {
#endif
			dev_info(dev,
				 "%s :dma coherent mem declare %pa,%zu\n",
				 co->name, &co->base, co->size);
			co->init_done = true;
			err = 0;
		} else
			dev_err(dev,
				"%s :dma coherent mem declare fail %pa,%zu,err:%d\n",
				co->name, &co->base, co->size, err);
	} else {
		/*
		 * When vpr memory is reserved, kmemleak tries to scan vpr
		 * memory for pointers. vpr memory should not be accessed
		 * from cpu so avoid scanning it. When vpr memory is removed,
		 * the memblock_remove() API ensures that kmemleak won't scan
		 * a removed block.
		 */
		if (!strncmp(co->name, "vpr", 3))
			kmemleak_no_scan(__va(co->base));

		co->dma_info->cma_dev = co->cma_dev;
		err = dma_declare_coherent_resizable_cma_memory(
				co->dma_dev, co->dma_info);
		if (err)
			dev_err(dev, "%s coherent memory declaration failed\n",
				     co->name);
		else
			co->init_done = true;
	}
	return err;
}

static void nvmap_co_device_release(struct reserved_mem *rmem,struct device *dev)
{
	struct nvmap_platform_carveout *co = rmem->priv;

	if (!co)
		return;

	if (co->usage_mask == NVMAP_HEAP_CARVEOUT_IVM)
		kfree(co->name);
}

static const struct reserved_mem_ops nvmap_co_ops = {
	.device_init	= nvmap_co_device_init,
	.device_release	= nvmap_co_device_release,
};

int __init nvmap_co_setup(struct reserved_mem *rmem)
{
	struct nvmap_platform_carveout *co;
	int ret = 0;
	struct cma *cma;
	ulong start = sched_clock();

	co = nvmap_get_carveout_pdata(rmem->name);
	if (!co)
		return ret;

	rmem->ops = &nvmap_co_ops;
	rmem->priv = co;

	/* IVM carveouts */
	if (!co->name)
		goto finish;

	co->base = rmem->base;
	co->size = rmem->size;

	if (!of_get_flat_dt_prop(rmem->fdt_node, "reusable", NULL) ||
	    of_get_flat_dt_prop(rmem->fdt_node, "no-map", NULL))
		goto skip_cma;

	WARN_ON(!rmem->base);
	if (dev_get_cma_area(co->cma_dev)) {
		pr_info("cma area initialed in legacy way already\n");
		goto finish;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
	ret = cma_init_reserved_mem(rmem->base, rmem->size, 0,
					rmem->name, &cma);
#else
	ret = cma_init_reserved_mem(rmem->base, rmem->size, 0, &cma);
#endif
	if (ret) {
		pr_info("cma_init_reserved_mem fails for %s\n", rmem->name);
		goto finish;
	}

	dma_contiguous_early_fixup(rmem->base, rmem->size);
	dev_set_cma_area(co->cma_dev, cma);
	pr_debug("tegra-carveouts carveout=%s %pa@%pa\n",
		 rmem->name, &rmem->size, &rmem->base);
	goto finish;

skip_cma:
	co->cma_dev = NULL;
finish:
	nvmap_init_time += sched_clock() - start;
	return ret;
}
RESERVEDMEM_OF_DECLARE(nvmap_co, "nvidia,generic_carveout", nvmap_co_setup);
RESERVEDMEM_OF_DECLARE(nvmap_ivm_co, "nvidia,ivm_carveout", nvmap_co_setup);
RESERVEDMEM_OF_DECLARE(nvmap_iram_co, "nvidia,iram-carveout", nvmap_co_setup);
RESERVEDMEM_OF_DECLARE(nvmap_vpr_co, "nvidia,vpr-carveout", nvmap_co_setup);

/*
 * This requires proper kernel arguments to have been passed.
 */
static int __nvmap_init_legacy(struct device *dev)
{
	/* Carveout. */
	if (!nvmap_carveouts[1].base) {
		nvmap_carveouts[1].base = tegra_carveout_start;
		nvmap_carveouts[1].size = tegra_carveout_size;
		if (!tegra_vpr_resize)
			nvmap_carveouts[1].cma_dev = NULL;
	}

	/* VPR */
	if (!nvmap_carveouts[2].base) {
		nvmap_carveouts[2].base = tegra_vpr_start;
		nvmap_carveouts[2].size = tegra_vpr_size;
		if (!tegra_vpr_resize)
			nvmap_carveouts[2].cma_dev = NULL;
	}

	return 0;
}

/*
 * Fills in the platform data either from the device tree or with the
 * legacy path.
 */
int __init nvmap_init(struct platform_device *pdev)
{
	int err;
	struct reserved_mem rmem;

	if (pdev->dev.of_node) {
		err = __nvmap_init_dt(pdev);
		if (err)
			return err;
	}

	err = of_reserved_mem_device_init(&pdev->dev);
	if (err)
		pr_debug("reserved_mem_device_init fails, try legacy init\n");

	/* try legacy init */
	if (!nvmap_carveouts[1].init_done) {
		rmem.priv = &nvmap_carveouts[1];
		err = nvmap_co_device_init(&rmem, &pdev->dev);
		if (err)
			goto end;
	}

	if (!nvmap_carveouts[2].init_done) {
		rmem.priv = &nvmap_carveouts[2];
		err = nvmap_co_device_init(&rmem, &pdev->dev);
	}

end:
	return err;
}

static struct platform_driver __refdata nvmap_driver = {
	.probe		= nvmap_probe,
	.remove		= nvmap_remove,

	.driver = {
		.name	= "tegra-carveouts",
		.owner	= THIS_MODULE,
		.of_match_table = nvmap_of_ids,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.suppress_bind_attrs = true,
	},
};

static int __init nvmap_init_driver(void)
{
	int e = 0;

	e = nvmap_heap_init();
	if (e)
		goto fail;

	e = platform_driver_register(&nvmap_driver);
	if (e) {
		nvmap_heap_deinit();
		goto fail;
	}

fail:
	return e;
}
fs_initcall(nvmap_init_driver);

static void __exit nvmap_exit_driver(void)
{
	platform_driver_unregister(&nvmap_driver);
	nvmap_heap_deinit();
	nvmap_dev = NULL;
}
module_exit(nvmap_exit_driver);
