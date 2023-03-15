/*
 * drivers/video/tegra/nvmap/nvmap_heap.c
 *
 * GPU heap allocator.
 *
 * Copyright (c) 2011-2022, NVIDIA Corporation. All rights reserved.
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

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/stat.h>
#include <linux/sizes.h>
#include <linux/io.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/sched/clock.h>
#endif

#include <linux/nvmap.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>

#include "nvmap_priv.h"
#include "nvmap_heap.h"

/*
 * "carveouts" are platform-defined regions of physically contiguous memory
 * which are not managed by the OS. A platform may specify multiple carveouts,
 * for either small special-purpose memory regions (like IRAM on Tegra SoCs)
 * or reserved regions of main system memory.
 *
 * The carveout allocator returns allocations which are physically contiguous.
 */

static struct kmem_cache *heap_block_cache;

struct list_block {
	struct nvmap_heap_block block;
	struct list_head all_list;
	unsigned int mem_prot;
	phys_addr_t orig_addr;
	size_t size;
	size_t align;
	struct nvmap_heap *heap;
	struct list_head free_list;
};

struct nvmap_heap {
	struct list_head all_list;
	struct mutex lock;
	const char *name;
	void *arg;
	/* heap base */
	phys_addr_t base;
	/* heap size */
	size_t len;
	struct device *cma_dev;
	struct device *dma_dev;
	bool is_ivm;
	bool can_alloc;  /* Used only if is_ivm == true */
	int peer;        /* Used only if is_ivm == true */
	int vm_id;       /* Used only if is_ivm == true */
	bool is_ivm_vpr; /* Used only if is_ivm == true */
	struct nvmap_pm_ops pm_ops;
};

struct device *dma_dev_from_handle(unsigned long type)
{
	int i;
	struct nvmap_carveout_node *co_heap;

	for (i = 0; i < nvmap_dev->nr_carveouts; i++) {
		co_heap = &nvmap_dev->heaps[i];

		if (!(co_heap->heap_bit & type))
			continue;

		return co_heap->carveout->dma_dev;
	}
	return ERR_PTR(-ENODEV);
}

int nvmap_query_heap_peer(struct nvmap_heap *heap)
{
	if (!heap || !heap->is_ivm)
		return -EINVAL;

	return heap->peer;
}

size_t nvmap_query_heap_size(struct nvmap_heap *heap)
{
	if (!heap)
		return -EINVAL;

	return heap->len;
}

void nvmap_heap_debugfs_init(struct dentry *heap_root, struct nvmap_heap *heap)
{
	if (sizeof(heap->base) == sizeof(u64))
		debugfs_create_x64("base", S_IRUGO,
			heap_root, (u64 *)&heap->base);
	else
		debugfs_create_x32("base", S_IRUGO,
			heap_root, (u32 *)&heap->base);
	if (sizeof(heap->len) == sizeof(u64))
		debugfs_create_x64("size", S_IRUGO,
			heap_root, (u64 *)&heap->len);
	else
		debugfs_create_x32("size", S_IRUGO,
			heap_root, (u32 *)&heap->len);
}

static phys_addr_t nvmap_alloc_mem(struct nvmap_heap *h, size_t len,
				   phys_addr_t *start)
{
	phys_addr_t pa;
	DEFINE_DMA_ATTRS(attrs);
	struct device *dev = h->dma_dev;

	dma_set_attr(DMA_ATTR_ALLOC_EXACT_SIZE, __DMA_ATTR(attrs));

#ifdef CONFIG_TEGRA_VIRTUALIZATION
	if (start && h->is_ivm) {
		void *ret;
		pa = h->base + (*start);
		ret = dma_mark_declared_memory_occupied(dev, pa, len,
					__DMA_ATTR(attrs));
		if (IS_ERR(ret)) {
			dev_err(dev, "Failed to reserve (%pa) len(%zu)\n",
					&pa, len);
			return DMA_ERROR_CODE;
		} else {
			dev_dbg(dev, "reserved (%pa) len(%zu)\n",
				&pa, len);
		}
	} else
#endif
	{
		(void)dma_alloc_attrs(dev, len, &pa,
				GFP_KERNEL, __DMA_ATTR(attrs));
		if (!dma_mapping_error(dev, pa)) {
			int ret;

			dev_dbg(dev, "Allocated addr (%pa) len(%zu)\n",
					&pa, len);
			if (!dma_is_coherent_dev(dev) && h->cma_dev) {
				ret = nvmap_cache_maint_phys_range(
					NVMAP_CACHE_OP_WB, pa, pa + len,
					true, true);
				if (!ret)
					return pa;

				dev_err(dev, "cache WB on (%pa, %zu) failed\n",
					&pa, len);
			}
		}
	}

	return pa;
}

static void nvmap_free_mem(struct nvmap_heap *h, phys_addr_t base,
				size_t len)
{
	struct device *dev = h->dma_dev;
	DEFINE_DMA_ATTRS(attrs);

	dma_set_attr(DMA_ATTR_ALLOC_EXACT_SIZE, __DMA_ATTR(attrs));
	dev_dbg(dev, "Free base (%pa) size (%zu)\n", &base, len);
#ifdef CONFIG_TEGRA_VIRTUALIZATION
	if (h->is_ivm && !h->can_alloc) {
		dma_mark_declared_memory_unoccupied(dev, base, len, __DMA_ATTR(attrs));
	} else
#endif
	{
		dma_free_attrs(dev, len,
			        (void *)(uintptr_t)base,
			        (dma_addr_t)base, __DMA_ATTR(attrs));
	}
}

/*
 * base_max limits position of allocated chunk in memory.
 * if base_max is 0 then there is no such limitation.
 */
static struct nvmap_heap_block *do_heap_alloc(struct nvmap_heap *heap,
					      size_t len, size_t align,
					      unsigned int mem_prot,
					      phys_addr_t base_max,
					      phys_addr_t *start)
{
	struct list_block *heap_block = NULL;
	dma_addr_t dev_base;
	struct device *dev = heap->dma_dev;

	/* since pages are only mappable with one cache attribute,
	 * and most allocations from carveout heaps are DMA coherent
	 * (i.e., non-cacheable), round cacheable allocations up to
	 * a page boundary to ensure that the physical pages will
	 * only be mapped one way. */
	if (mem_prot == NVMAP_HANDLE_CACHEABLE ||
	    mem_prot == NVMAP_HANDLE_INNER_CACHEABLE) {
		align = max_t(size_t, align, PAGE_SIZE);
		len = PAGE_ALIGN(len);
	}

	if (heap->is_ivm)
		align = max_t(size_t, align, NVMAP_IVM_ALIGNMENT);

	heap_block = kmem_cache_zalloc(heap_block_cache, GFP_KERNEL);
	if (!heap_block) {
		dev_err(dev, "%s: failed to alloc heap block %s\n",
			__func__, dev_name(dev));
		goto fail_heap_block_alloc;
	}

	dev_base = nvmap_alloc_mem(heap, len, start);
	if (dma_mapping_error(dev, dev_base)) {
		dev_err(dev, "failed to alloc mem of size (%zu)\n",
			len);
		if (dma_is_coherent_dev(dev)) {
			struct dma_coherent_stats stats;

			dma_get_coherent_stats(dev, &stats);
			dev_err(dev, "used:%zu,curr_size:%zu max:%zu\n",
				stats.used, stats.size, stats.max);
		}
		goto fail_dma_alloc;
	}

	heap_block->block.base = dev_base;
	heap_block->orig_addr = dev_base;
	heap_block->size = len;

	list_add_tail(&heap_block->all_list, &heap->all_list);
	heap_block->heap = heap;
	heap_block->mem_prot = mem_prot;
	heap_block->align = align;
	return &heap_block->block;

fail_dma_alloc:
	kmem_cache_free(heap_block_cache, heap_block);
fail_heap_block_alloc:
	return NULL;
}

static struct list_block *do_heap_free(struct nvmap_heap_block *block)
{
	struct list_block *b = container_of(block, struct list_block, block);
	struct nvmap_heap *heap = b->heap;

	list_del(&b->all_list);

	nvmap_free_mem(heap, block->base, b->size);
	kmem_cache_free(heap_block_cache, b);

	return b;
}

/* nvmap_heap_alloc: allocates a block of memory of len bytes, aligned to
 * align bytes. */
struct nvmap_heap_block *nvmap_heap_alloc(struct nvmap_heap *h,
					  struct nvmap_handle *handle,
					  phys_addr_t *start)
{
	struct nvmap_heap_block *b;
	size_t len        = handle->size;
	size_t align      = handle->align;
	unsigned int prot = handle->flags;

	mutex_lock(&h->lock);

	if (h->is_ivm) { /* Is IVM carveout? */
		/* Check if this correct IVM heap */
		if (handle->peer != h->peer) {
			mutex_unlock(&h->lock);
			return NULL;
		} else {
			if (h->can_alloc && start) {
				/* If this partition does actual allocation, it
				 * should not specify start_offset.
				 */
				mutex_unlock(&h->lock);
				return NULL;
			} else if (!h->can_alloc && !start) {
				/* If this partition does not do actual
				 * allocation, it should specify start_offset.
				 */
				mutex_unlock(&h->lock);
				return NULL;
			}
		}
	}

	/*
	 * If this HEAP has pm_ops defined and powering on the
	 * RAM attached with the HEAP returns error, don't
	 * allocate from the heap and return NULL.
	 */
	if (h->pm_ops.busy) {
		if (h->pm_ops.busy() < 0) {
			pr_err("Unable to power on the heap device\n");
			mutex_unlock(&h->lock);
			return NULL;
		}
	}

	align = max_t(size_t, align, L1_CACHE_BYTES);
	b = do_heap_alloc(h, len, align, prot, 0, start);
	if (b) {
		b->handle = handle;
		handle->carveout = b;
		/* Generate IVM for partition that can alloc */
		if (h->is_ivm && h->can_alloc) {
			u64 size_temp = len;

			/* h->base is the address of the whole IVM carveout.
			 * b->base is an address of an allocation region that
			 * lives inside the carveout.
			 */
			u64 offs_temp = b->base - h->base;
			u64 peer_temp = h->vm_id;

			/* Is offset NVMAP_IVM_ALIGNMENT aligned? */
			BUG_ON(offs_temp & (NVMAP_IVM_ALIGNMENT - 1));

			/* Is size PAGE aligned */
			BUG_ON(size_temp & ~PAGE_MASK);

			// Offset as multiples of NVMAP_IVM_ALIGNMENT.
			offs_temp >>= (ffs(NVMAP_IVM_ALIGNMENT) - 1);

			// Size as multiples of a PAGE.
			size_temp >>= PAGE_SHIFT;

			// Check they fit into their bitfields.
			BUG_ON((size_temp << NVMAP_IVM_SIZE_SHIFT) &
				~(NVMAP_IVM_SIZE_MASK));
			BUG_ON((offs_temp << NVMAP_IVM_OFFSET_SHIFT) &
				~(NVMAP_IVM_OFFSET_MASK));
			BUG_ON((peer_temp << NVMAP_IVM_PEER_SHIFT) &
				~(NVMAP_IVM_PEER_MASK));

			handle->ivm_id = size_temp << NVMAP_IVM_SIZE_SHIFT;
			handle->ivm_id |= offs_temp << NVMAP_IVM_OFFSET_SHIFT;
			handle->ivm_id |= peer_temp << NVMAP_IVM_PEER_SHIFT;

			if (h->is_ivm_vpr)
				handle->ivm_id |= 1ULL << NVMAP_IVM_ISVPR_SHIFT;
		}
	}
	mutex_unlock(&h->lock);
	return b;
}

struct nvmap_heap *nvmap_block_to_heap(struct nvmap_heap_block *b)
{
	struct list_block *lb;
	lb = container_of(b, struct list_block, block);
	return lb->heap;
}

/* nvmap_heap_free: frees block b*/
void nvmap_heap_free(struct nvmap_heap_block *b)
{
	struct nvmap_heap *h;
	struct list_block *lb;

	if (!b)
		return;

	h = nvmap_block_to_heap(b);
	mutex_lock(&h->lock);

	lb = container_of(b, struct list_block, block);
	nvmap_flush_heap_block(NULL, b, lb->size, lb->mem_prot);
	do_heap_free(b);
	/*
	 * If this HEAP has pm_ops defined and powering off the
	 * RAM attached with the HEAP returns error, raise warning.
	 */
	if (h->pm_ops.idle) {
		if (h->pm_ops.idle() < 0)
			WARN_ON(1);
	}

	mutex_unlock(&h->lock);
}

/* nvmap_heap_create: create a heap object of len bytes, starting from
 * address base.
 */
struct nvmap_heap *nvmap_heap_create(struct device *parent,
				     const struct nvmap_platform_carveout *co,
				     phys_addr_t base, size_t len, void *arg)
{
	struct nvmap_heap *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) {
		dev_err(parent, "%s: out of memory\n", __func__);
		return NULL;
	}

	h->dma_dev = co->dma_dev;
	if (co->cma_dev) {
#ifdef CONFIG_DMA_CMA
		struct dma_contiguous_stats stats;

		if (dma_get_contiguous_stats(co->cma_dev, &stats))
			goto fail;

		base = stats.base;
		len = stats.size;
		h->cma_dev = co->cma_dev;
#else
		dev_err(parent, "invalid resize config for carveout %s\n",
				co->name);
		goto fail;
#endif
	} else if (!co->init_done) {
		int err;

		/* declare Non-CMA heap */
		err = dma_declare_coherent_memory(h->dma_dev, 0, base, len,
				DMA_MEMORY_NOMAP | DMA_MEMORY_EXCLUSIVE);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
		if (!err) {
#else
		if (err & DMA_MEMORY_NOMAP) {
#endif
			dev_info(parent,
				"%s :dma coherent mem declare %pa,%zu\n",
				co->name, &base, len);
		} else {
			dev_err(parent,
				"%s: dma coherent declare fail %pa,%zu\n",
				co->name, &base, len);
			goto fail;
		}
	}

	dev_set_name(h->dma_dev, "%s", co->name);
	dma_set_coherent_mask(h->dma_dev, DMA_BIT_MASK(64));
	h->name = co->name;
	h->arg = arg;
	h->base = base;
	h->can_alloc = !!co->can_alloc;
	h->is_ivm = co->is_ivm;
	h->is_ivm_vpr = (co->usage_mask == NVMAP_HEAP_CARVEOUT_IVM_VPR);
	BUG_ON(h->is_ivm_vpr && !h->is_ivm);
	h->len = len;
	h->peer = co->peer;
	h->vm_id = co->vmid;

	if (co->pm_ops.busy)
		h->pm_ops.busy = co->pm_ops.busy;

	if (co->pm_ops.idle)
		h->pm_ops.idle = co->pm_ops.idle;

	INIT_LIST_HEAD(&h->all_list);
	mutex_init(&h->lock);
	if (!co->no_cpu_access &&
		nvmap_cache_maint_phys_range(NVMAP_CACHE_OP_WB_INV,
				base, base + len, true, true)) {
		dev_err(parent, "cache flush failed\n");
		goto fail;
	}
	wmb();

	if (co->disable_dynamic_dma_map)
		nvmap_dev->dynamic_dma_map_mask &= ~co->usage_mask;

	if (co->no_cpu_access)
		nvmap_dev->cpu_access_mask &= ~co->usage_mask;

	dev_info(parent, "created heap %s base 0x%p size (%zuKiB)\n",
		co->name, (void *)(uintptr_t)base, len/1024);
	return h;
fail:
	kfree(h);
	return NULL;
}

/* nvmap_heap_destroy: frees all resources in heap */
void nvmap_heap_destroy(struct nvmap_heap *heap)
{
	WARN_ON(!list_is_singular(&heap->all_list));
	while (!list_empty(&heap->all_list)) {
		struct list_block *l;
		l = list_first_entry(&heap->all_list, struct list_block,
				     all_list);
		list_del(&l->all_list);
		kmem_cache_free(heap_block_cache, l);
	}
	kfree(heap);
}

int nvmap_heap_init(void)
{
	ulong start_time = sched_clock();

	heap_block_cache = KMEM_CACHE(list_block, 0);
	if (!heap_block_cache) {
		pr_err("%s: unable to create heap block cache\n", __func__);
		return -ENOMEM;
	}
	pr_info("%s: created heap block cache\n", __func__);
	nvmap_init_time += sched_clock() - start_time;
	return 0;
}

void nvmap_heap_deinit(void)
{
	if (heap_block_cache)
		kmem_cache_destroy(heap_block_cache);

	heap_block_cache = NULL;
}

/*
 * This routine is used to flush the carveout memory from cache.
 * Why cache flush is needed for carveout? Consider the case, where a piece of
 * carveout is allocated as cached and released. After this, if the same memory is
 * allocated for uncached request and the memory is not flushed out from cache.
 * In this case, the client might pass this to H/W engine and it could start modify
 * the memory. As this was cached earlier, it might have some portion of it in cache.
 * During cpu request to read/write other memory, the cached portion of this memory
 * might get flushed back to main memory and would cause corruptions, if it happens
 * after H/W writes data to memory.
 *
 * But flushing out the memory blindly on each carveout allocation is redundant.
 *
 * In order to optimize the carveout buffer cache flushes, the following
 * strategy is used.
 *
 * The whole Carveout is flushed out from cache during its initialization.
 * During allocation, carveout buffers are not flused from cache.
 * During deallocation, carveout buffers are flushed, if they were allocated as cached.
 * if they were allocated as uncached/writecombined, no cache flush is needed.
 * Just draining store buffers is enough.
 */
int nvmap_flush_heap_block(struct nvmap_client *client,
	struct nvmap_heap_block *block, size_t len, unsigned int prot)
{
	phys_addr_t phys = block->base;
	phys_addr_t end = block->base + len;
	int ret = 0;

	if (prot == NVMAP_HANDLE_UNCACHEABLE || prot == NVMAP_HANDLE_WRITE_COMBINE)
		goto out;

	ret = nvmap_cache_maint_phys_range(NVMAP_CACHE_OP_WB_INV, phys, end,
				true, prot != NVMAP_HANDLE_INNER_CACHEABLE);
	if (ret)
		goto out;
out:
	wmb();
	return ret;
}
