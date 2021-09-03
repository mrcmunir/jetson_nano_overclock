/*
 * drivers/video/tegra/nvmap/nvmap.h
 *
 * GPU memory management driver for Tegra
 *
 * Copyright (c) 2009-2021, NVIDIA CORPORATION. All rights reserved.
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

#ifndef __VIDEO_TEGRA_NVMAP_NVMAP_H
#define __VIDEO_TEGRA_NVMAP_NVMAP_H

#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rtmutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/dma-buf.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/nvmap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direction.h>
#include <linux/platform_device.h>
#include <linux/tegra-mce.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>

#include <asm/cacheflush.h>
#ifndef CONFIG_ARM64
#include <asm/outercache.h>
#endif
#include "nvmap_heap.h"
#include "nvmap_stats.h"

#define NVMAP_TAG_LABEL_MAXLEN	(63 - sizeof(struct nvmap_tag_entry))

#define NVMAP_TP_ARGS_H(handle)					      	      \
	handle,								      \
	atomic_read(&handle->share_count),				      \
	handle->heap_type == NVMAP_HEAP_IOVMM ? 0 : 			      \
			(handle->carveout ? handle->carveout->base : 0),      \
	handle->size,							      \
	(handle->userflags & 0xFFFF),                                         \
	(handle->userflags >> 16),					      \
	__nvmap_tag_name(nvmap_dev, handle->userflags >> 16)

#define NVMAP_TP_ARGS_CHR(client, handle, ref)			      	      \
	client,                                                               \
	client ? nvmap_client_pid((struct nvmap_client *)client) : 0,         \
	(ref) ? atomic_read(&((struct nvmap_handle_ref *)ref)->dupes) : 1,    \
	NVMAP_TP_ARGS_H(handle)

#define NVMAP_TAG_TRACE(x, ...) 			\
do {                                                    \
	if (x##_enabled()) {                            \
		mutex_lock(&nvmap_dev->tags_lock);      \
		x(__VA_ARGS__);                         \
		mutex_unlock(&nvmap_dev->tags_lock);    \
	}                                               \
} while (0)

#define GFP_NVMAP       (GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN)

struct page;
struct nvmap_device;

void _nvmap_handle_free(struct nvmap_handle *h);
/* holds max number of handles allocted per process at any time */
extern u32 nvmap_max_handle_count;
extern u64 nvmap_big_page_allocs;
extern u64 nvmap_total_page_allocs;

extern bool nvmap_convert_iovmm_to_carveout;
extern bool nvmap_convert_carveout_to_iovmm;

extern struct vm_operations_struct nvmap_vma_ops;

#ifdef CONFIG_ARM64
#define PG_PROT_KERNEL PAGE_KERNEL
#define FLUSH_DCACHE_AREA __flush_dcache_area
#define outer_flush_range(s, e)
#define outer_inv_range(s, e)
#define outer_clean_range(s, e)
#define outer_flush_all()
#define outer_clean_all()
extern void __clean_dcache_page(struct page *);
#else
#define PG_PROT_KERNEL pgprot_kernel
#define FLUSH_DCACHE_AREA __cpuc_flush_dcache_area
extern void __flush_dcache_page(struct address_space *, struct page *);
#endif

struct nvmap_vma_list {
	struct list_head list;
	struct vm_area_struct *vma;
	unsigned long save_vm_flags;
	pid_t pid;
	atomic_t ref;
};

struct nvmap_carveout_node {
	unsigned int		heap_bit;
	struct nvmap_heap	*carveout;
	int			index;
	phys_addr_t		base;
	size_t			size;
};

/* handles allocated using shared system memory (either IOVMM- or high-order
 * page allocations */
struct nvmap_pgalloc {
	struct page **pages;
	bool contig;			/* contiguous system memory */
	atomic_t reserved;
	atomic_t ndirty;	/* count number of dirty pages */
};

/* bit 31-29: IVM peer
 * bit 28-16: offset (aligned to 32K)
 * bit 15-00: len (aligned to page_size)
 */
#define NVMAP_IVM_LENGTH_SHIFT (0)
#define NVMAP_IVM_LENGTH_WIDTH (16)
#define NVMAP_IVM_LENGTH_MASK  ((1 << NVMAP_IVM_LENGTH_WIDTH) - 1)
#define NVMAP_IVM_OFFSET_SHIFT (NVMAP_IVM_LENGTH_SHIFT + NVMAP_IVM_LENGTH_WIDTH)
#define NVMAP_IVM_OFFSET_WIDTH (14)
#define NVMAP_IVM_OFFSET_MASK  ((1 << NVMAP_IVM_OFFSET_WIDTH) - 1)
#define NVMAP_IVM_IVMID_SHIFT  (NVMAP_IVM_OFFSET_SHIFT + NVMAP_IVM_OFFSET_WIDTH)
#define NVMAP_IVM_IVMID_WIDTH  (3)
#define NVMAP_IVM_IVMID_MASK   ((1 << NVMAP_IVM_IVMID_WIDTH) - 1)
#define NVMAP_IVM_ALIGNMENT    (SZ_32K)

struct nvmap_handle_dmabuf_priv {
	void *priv;
	struct device *dev;
	void (*priv_release)(void *priv);
	struct list_head list;
};

struct nvmap_handle {
	struct rb_node node;	/* entry on global handle tree */
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	atomic_t pin;		/* pin count */
	u32 flags;		/* caching flags */
	size_t size;		/* padded (as-allocated) size */
	size_t orig_size;	/* original (as-requested) size */
	size_t align;
	struct nvmap_client *owner;
	struct dma_buf *dmabuf;
	union {
		struct nvmap_pgalloc pgalloc;
		struct nvmap_heap_block *carveout;
	};
	bool heap_pgalloc;	/* handle is page allocated (sysmem / iovmm) */
	bool alloc;		/* handle has memory allocated */
	bool from_va;		/* handle memory is from VA */
	u32 heap_type;		/* handle heap is allocated from */
	u32 userflags;		/* flags passed from userspace */
	void *vaddr;		/* mapping used inside kernel */
	struct list_head vmas;	/* list of all user vma's */
	atomic_t umap_count;	/* number of outstanding maps from user */
	atomic_t kmap_count;	/* number of outstanding map from kernel */
	atomic_t share_count;	/* number of processes sharing the handle */
	struct list_head lru;	/* list head to track the lru */
	struct mutex lock;
	struct list_head dmabuf_priv;
	u64 ivm_id;
	int peer;		/* Peer VM number */
	bool is_ro;		/* Is handle read-only? */
};

struct nvmap_handle_info {
	struct nvmap_handle *handle;
	struct list_head maps;
	struct mutex maps_lock;
};

struct nvmap_tag_entry {
	struct rb_node node;
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	u32 tag;
};

/* handle_ref objects are client-local references to an nvmap_handle;
 * they are distinct objects so that handles can be unpinned and
 * unreferenced the correct number of times when a client abnormally
 * terminates */
struct nvmap_handle_ref {
	struct nvmap_handle *handle;
	struct rb_node	node;
	atomic_t	dupes;	/* number of times to free on file close */
};

#ifdef CONFIG_NVMAP_PAGE_POOLS
/*
 * This is the default ratio defining pool size. It can be thought of as pool
 * size in either MB per GB or KB per MB. That means the max this number can
 * be is 1024 (all physical memory - not a very good idea) or 0 (no page pool
 * at all).
 */
#define NVMAP_PP_POOL_SIZE               (128)

#define NVMAP_PP_BIG_PAGE_SIZE           (0x10000)

struct nvmap_page_pool {
	struct rt_mutex lock;
	u32 count;      /* Number of pages in the page & dirty list. */
	u32 max;        /* Max no. of pages in all lists. */
	u32 to_zero;    /* Number of pages on the zero list */
	u32 under_zero; /* Number of pages getting zeroed */
	u32 big_pg_sz;  /* big page size supported(64k, etc.) */
	u32 big_page_count;   /* Number of zeroed big pages avaialble */
	u32 pages_per_big_pg; /* Number of pages in big page */
	struct list_head page_list;
	struct list_head zero_list;
	struct list_head page_list_bp;

#ifdef CONFIG_NVMAP_PAGE_POOL_DEBUG
	u64 allocs;
	u64 fills;
	u64 hits;
	u64 misses;
#endif
};

int nvmap_page_pool_init(struct nvmap_device *dev);
int nvmap_page_pool_fini(struct nvmap_device *dev);
struct page *nvmap_page_pool_alloc(struct nvmap_page_pool *pool);
int nvmap_page_pool_alloc_lots(struct nvmap_page_pool *pool,
					struct page **pages, u32 nr);
int nvmap_page_pool_alloc_lots_bp(struct nvmap_page_pool *pool,
					struct page **pages, u32 nr);
int nvmap_page_pool_fill_lots(struct nvmap_page_pool *pool,
				       struct page **pages, u32 nr);
int nvmap_page_pool_clear(void);
int nvmap_page_pool_debugfs_init(struct dentry *nvmap_root);
#endif

#define NVMAP_IVM_INVALID_PEER		(-1)

struct nvmap_client {
	const char			*name;
	struct rb_root			handle_refs;
	struct mutex			ref_lock;
	bool				kernel_client;
	atomic_t			count;
	struct task_struct		*task;
	struct list_head		list;
	u32				handle_count;
	u32				next_fd;
	int				warned;
	int				tag_warned;
};

struct nvmap_vma_priv {
	struct nvmap_handle *handle;
	size_t		offs;
	atomic_t	count;	/* number of processes cloning the VMA */
};

struct nvmap_device {
	struct rb_root	handles;
	spinlock_t	handle_lock;
	struct miscdevice dev_user;
	struct nvmap_carveout_node *heaps;
	int nr_heaps;
	int nr_carveouts;
#ifdef CONFIG_NVMAP_PAGE_POOLS
	struct nvmap_page_pool pool;
#endif
	struct list_head clients;
	struct rb_root pids;
	struct mutex	clients_lock;
	struct list_head lru_handles;
	spinlock_t	lru_lock;
	struct dentry *handles_by_pid;
	struct dentry *debug_root;
	struct nvmap_platform_data *plat;
	struct rb_root	tags;
	struct mutex	tags_lock;
	u32 dynamic_dma_map_mask;
	u32 cpu_access_mask;
};


extern struct nvmap_device *nvmap_dev;
extern ulong nvmap_init_time;

static inline void nvmap_ref_lock(struct nvmap_client *priv)
{
	mutex_lock(&priv->ref_lock);
}

static inline void nvmap_ref_unlock(struct nvmap_client *priv)
{
	mutex_unlock(&priv->ref_lock);
}

/*
 * NOTE: this does not ensure the continued existence of the underlying
 * dma_buf. If you want ensure the existence of the dma_buf you must get an
 * nvmap_handle_ref as that is what tracks the dma_buf refs.
 */
static inline struct nvmap_handle *nvmap_handle_get(struct nvmap_handle *h)
{
	if (WARN_ON(!virt_addr_valid(h))) {
		pr_err("%s: invalid handle\n", current->group_leader->comm);
		return NULL;
	}

	if (unlikely(atomic_inc_return(&h->ref) <= 1)) {
		pr_err("%s: %s attempt to get a freed handle\n",
			__func__, current->group_leader->comm);
		atomic_dec(&h->ref);
		return NULL;
	}
	return h;
}

static inline pgprot_t nvmap_pgprot(struct nvmap_handle *h, pgprot_t prot)
{
	if (h->flags == NVMAP_HANDLE_UNCACHEABLE) {
#ifdef CONFIG_ARM64
		if (h->heap_type != NVMAP_HEAP_CARVEOUT_VPR &&
		    h->owner && !h->owner->warned) {
			char task_comm[TASK_COMM_LEN];
			h->owner->warned = 1;
			get_task_comm(task_comm, h->owner->task);
			pr_err("PID %d: %s: TAG: 0x%04x WARNING: "
				"NVMAP_HANDLE_WRITE_COMBINE "
				"should be used in place of "
				"NVMAP_HANDLE_UNCACHEABLE on ARM64\n",
				h->owner->task->pid, task_comm,
				h->userflags >> 16);
		}
#endif
		return pgprot_noncached(prot);
	}
	else if (h->flags == NVMAP_HANDLE_WRITE_COMBINE)
		return pgprot_writecombine(prot);
	return prot;
}

int nvmap_probe(struct platform_device *pdev);
int nvmap_remove(struct platform_device *pdev);
int nvmap_init(struct platform_device *pdev);

int nvmap_create_carveout(const struct nvmap_platform_carveout *co);
int nvmap_co_setup(struct reserved_mem *rmem);

struct device *dma_dev_from_handle(unsigned long type);
struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *dev,
					      struct nvmap_handle *handle,
					      unsigned long type,
					      phys_addr_t *start);

struct nvmap_carveout_node;

void nvmap_handle_put(struct nvmap_handle *h);

struct nvmap_handle_ref *__nvmap_validate_locked(struct nvmap_client *priv,
						 struct nvmap_handle *h);

struct nvmap_handle *nvmap_validate_get(struct nvmap_handle *h);

struct nvmap_handle_ref *nvmap_create_handle(struct nvmap_client *client,
					     size_t size, bool ro_buf);

struct nvmap_handle_ref *nvmap_create_handle_from_va(struct nvmap_client *client,
						     ulong addr, size_t size,
						     unsigned int access_flags);

struct nvmap_handle_ref *nvmap_duplicate_handle(struct nvmap_client *client,
					struct nvmap_handle *h, bool skip_val);

struct nvmap_handle_ref *nvmap_try_duplicate_by_ivmid(
			struct nvmap_client *client, u64 ivm_id,
			struct nvmap_heap_block **block);

struct nvmap_handle_ref *nvmap_create_handle_from_fd(
			struct nvmap_client *client, int fd);

void inner_cache_maint(unsigned int op, void *vaddr, size_t size);
void outer_cache_maint(unsigned int op, phys_addr_t paddr, size_t size);

int nvmap_alloc_handle(struct nvmap_client *client,
		       struct nvmap_handle *h, unsigned int heap_mask,
		       size_t align, u8 kind,
		       unsigned int flags, int peer);

int nvmap_alloc_handle_from_va(struct nvmap_client *client,
			       struct nvmap_handle *h,
			       ulong addr,
			       unsigned int flags);

void nvmap_free_handle(struct nvmap_client *c, struct nvmap_handle *h);

void nvmap_free_handle_fd(struct nvmap_client *c, int fd);

int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h);

void nvmap_handle_add(struct nvmap_device *dev, struct nvmap_handle *h);

int is_nvmap_vma(struct vm_area_struct *vma);

int nvmap_get_dmabuf_fd(struct nvmap_client *client, struct nvmap_handle *h);
struct nvmap_handle *nvmap_handle_get_from_dmabuf_fd(
				struct nvmap_client *client, int fd);
int nvmap_dmabuf_duplicate_gen_fd(struct nvmap_client *client,
		struct dma_buf *dmabuf);
struct sg_table *nvmap_dmabuf_map_dma_buf(
	struct dma_buf_attachment *attach, enum dma_data_direction dir);
struct sg_table *_nvmap_dmabuf_map_dma_buf(
	struct dma_buf_attachment *attach, enum dma_data_direction dir);
void nvmap_dmabuf_unmap_dma_buf(struct dma_buf_attachment *attach,
				       struct sg_table *sgt,
				       enum dma_data_direction dir);
void _nvmap_dmabuf_unmap_dma_buf(struct dma_buf_attachment *attach,
				       struct sg_table *sgt,
				       enum dma_data_direction dir);

int nvmap_get_handle_param(struct nvmap_client *client,
		struct nvmap_handle_ref *ref, u32 param, u64 *result);

struct nvmap_handle *nvmap_handle_get_from_fd(int fd);

/* MM definitions. */
extern size_t cache_maint_inner_threshold;
extern int nvmap_cache_maint_by_set_ways;

extern void v7_flush_kern_cache_all(void);
extern void v7_clean_kern_cache_all(void *);

extern void (*inner_flush_cache_all)(void);
extern void (*inner_clean_cache_all)(void);
void nvmap_override_cache_ops(void);
void nvmap_clean_cache(struct page **pages, int numpages);
void nvmap_clean_cache_page(struct page *page);
void nvmap_flush_cache(struct page **pages, int numpages);
int nvmap_cache_maint_phys_range(unsigned int op, phys_addr_t pstart,
		phys_addr_t pend, int inner, int outer);

int nvmap_do_cache_maint_list(struct nvmap_handle **handles, u64 *offsets,
			      u64 *sizes, int op, int nr, bool is_32);
int __nvmap_cache_maint(struct nvmap_client *client,
			       struct nvmap_cache_op_64 *op);
int nvmap_cache_debugfs_init(struct dentry *nvmap_root);

/* Internal API to support dmabuf */
struct dma_buf *__nvmap_dmabuf_export(struct nvmap_client *client,
				 struct nvmap_handle *handle);
struct dma_buf *__nvmap_make_dmabuf(struct nvmap_client *client,
				    struct nvmap_handle *handle, bool ro_buf);
struct sg_table *__nvmap_sg_table(struct nvmap_client *client,
				  struct nvmap_handle *h);
void __nvmap_free_sg_table(struct nvmap_client *client,
			   struct nvmap_handle *h, struct sg_table *sgt);
void *__nvmap_kmap(struct nvmap_handle *h, unsigned int pagenum);
void __nvmap_kunmap(struct nvmap_handle *h, unsigned int pagenum, void *addr);
void *__nvmap_mmap(struct nvmap_handle *h);
void __nvmap_munmap(struct nvmap_handle *h, void *addr);
int __nvmap_map(struct nvmap_handle *h, struct vm_area_struct *vma);
int __nvmap_do_cache_maint(struct nvmap_client *client, struct nvmap_handle *h,
			   unsigned long start, unsigned long end,
			   unsigned int op, bool clean_only_dirty);
struct nvmap_client *__nvmap_create_client(struct nvmap_device *dev,
					   const char *name);
int __nvmap_dmabuf_fd(struct nvmap_client *client,
		      struct dma_buf *dmabuf, int flags);

int nvmap_dmabuf_stash_init(void);

void *nvmap_altalloc(size_t len);
void nvmap_altfree(void *ptr, size_t len);

void do_set_pte(struct vm_area_struct *vma, unsigned long address,
		struct page *page, pte_t *pte, bool write, bool anon);

static inline struct page *nvmap_to_page(struct page *page)
{
	return (struct page *)((unsigned long)page & ~3UL);
}

static inline bool nvmap_page_dirty(struct page *page)
{
	return (unsigned long)page & 1UL;
}

static inline bool nvmap_page_mkdirty(struct page **page)
{
	if (nvmap_page_dirty(*page))
		return false;
	*page = (struct page *)((unsigned long)*page | 1UL);
	return true;
}

static inline bool nvmap_page_mkclean(struct page **page)
{
	if (!nvmap_page_dirty(*page))
		return false;
	*page = (struct page *)((unsigned long)*page & ~1UL);
	return true;
}

/*
 * FIXME: assume user space requests for reserve operations
 * are page aligned
 */
static inline int nvmap_handle_mk(struct nvmap_handle *h,
				  u32 offset, u32 size,
				  bool (*fn)(struct page **),
				  bool locked)
{
	int i, nchanged = 0;
	u32 start_page = offset >> PAGE_SHIFT;
	u32 end_page = PAGE_ALIGN(offset + size) >> PAGE_SHIFT;

	if (!locked)
		mutex_lock(&h->lock);
	if (h->heap_pgalloc &&
		(offset < h->size) &&
		(size <= h->size) &&
		(offset <= (h->size - size))) {
		for (i = start_page; i < end_page; i++)
			nchanged += fn(&h->pgalloc.pages[i]) ? 1 : 0;
	}
	if (!locked)
		mutex_unlock(&h->lock);
	return nchanged;
}

static inline void nvmap_handle_mkclean(struct nvmap_handle *h,
					u32 offset, u32 size)
{
	int nchanged;

	if (h->heap_pgalloc && !atomic_read(&h->pgalloc.ndirty))
		return;
	if (size == 0)
		size = h->size;

	nchanged = nvmap_handle_mk(h, offset, size, nvmap_page_mkclean, false);
	if (h->heap_pgalloc)
		atomic_sub(nchanged, &h->pgalloc.ndirty);
}

static inline void _nvmap_handle_mkdirty(struct nvmap_handle *h,
					u32 offset, u32 size)
{
	int nchanged;

	if (h->heap_pgalloc &&
		(atomic_read(&h->pgalloc.ndirty) == (h->size >> PAGE_SHIFT)))
		return;

	nchanged = nvmap_handle_mk(h, offset, size, nvmap_page_mkdirty, true);
	if (h->heap_pgalloc)
		atomic_add(nchanged, &h->pgalloc.ndirty);
}

static inline struct page **nvmap_pages(struct page **pg_pages, u32 nr_pages)
{
	struct page **pages;
	int i;

	pages = nvmap_altalloc(sizeof(*pages) * nr_pages);
	if (!pages)
		return NULL;

	for (i = 0; i < nr_pages; i++)
		pages[i] = nvmap_to_page(pg_pages[i]);

	return pages;
}

void nvmap_zap_handle(struct nvmap_handle *handle, u64 offset, u64 size);

void nvmap_vma_open(struct vm_area_struct *vma);

int nvmap_reserve_pages(struct nvmap_handle **handles, u64 *offsets,
			u64 *sizes, u32 nr, u32 op, bool is_32);

static inline void nvmap_kmaps_inc(struct nvmap_handle *h)
{
	mutex_lock(&h->lock);
	atomic_inc(&h->kmap_count);
	mutex_unlock(&h->lock);
}

static inline void nvmap_kmaps_inc_no_lock(struct nvmap_handle *h)
{
	atomic_inc(&h->kmap_count);
}

static inline void nvmap_kmaps_dec(struct nvmap_handle *h)
{
	atomic_dec(&h->kmap_count);
}

static inline void nvmap_umaps_inc(struct nvmap_handle *h)
{
	mutex_lock(&h->lock);
	atomic_inc(&h->umap_count);
	mutex_unlock(&h->lock);
}

static inline void nvmap_umaps_dec(struct nvmap_handle *h)
{
	atomic_dec(&h->umap_count);
}

static inline void nvmap_lru_add(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	BUG_ON(!list_empty(&h->lru));
	list_add_tail(&h->lru, &nvmap_dev->lru_handles);
	spin_unlock(&nvmap_dev->lru_lock);
}

static inline void nvmap_lru_del(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	list_del(&h->lru);
	INIT_LIST_HEAD(&h->lru);
	spin_unlock(&nvmap_dev->lru_lock);
}

static inline void nvmap_lru_reset(struct nvmap_handle *h)
{
	spin_lock(&nvmap_dev->lru_lock);
	BUG_ON(list_empty(&h->lru));
	list_del(&h->lru);
	list_add_tail(&h->lru, &nvmap_dev->lru_handles);
	spin_unlock(&nvmap_dev->lru_lock);
}

static inline bool nvmap_handle_track_dirty(struct nvmap_handle *h)
{
	if (!h->heap_pgalloc)
		return false;

	return h->userflags & (NVMAP_HANDLE_CACHE_SYNC |
			       NVMAP_HANDLE_CACHE_SYNC_AT_RESERVE);
}

struct nvmap_tag_entry *nvmap_search_tag_entry(struct rb_root *root, u32 tag);

int nvmap_define_tag(struct nvmap_device *dev, u32 tag,
	const char __user *name, u32 len);

int nvmap_remove_tag(struct nvmap_device *dev, u32 tag);

/* must hold tag_lock */
static inline char *__nvmap_tag_name(struct nvmap_device *dev, u32 tag)
{
	struct nvmap_tag_entry *entry;

	entry = nvmap_search_tag_entry(&dev->tags, tag);
	return entry ? (char *)(entry + 1) : "";
}
static inline pid_t nvmap_client_pid(struct nvmap_client *client)
{
	return client->task ? client->task->pid : 0;
}

static inline int nvmap_get_user_pages(ulong vaddr,
				int nr_page, struct page **pages,
				bool is_user_flags, u32 user_foll_flags)
{
	u32 foll_flags = FOLL_FORCE;
	struct vm_area_struct *vma;
	vm_flags_t vm_flags;
	int user_pages = 0;
	int ret = 0;

	down_read(&current->mm->mmap_sem);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
        user_pages = get_user_pages(current, current->mm,
			      vaddr & PAGE_MASK, nr_page,
			      1/*write*/, 1, /* force */
			      pages, NULL);
#else
	vma = find_vma(current->mm, vaddr);
	if (vma) {
		if (is_user_flags) {
			foll_flags |= user_foll_flags;
		} else {
			vm_flags = vma->vm_flags;
			/*
			 * If the vaddr points to writable page then only
			 * pass FOLL_WRITE flag
			 */
			if (vm_flags & VM_WRITE)
				foll_flags |= FOLL_WRITE;
		}
		pr_debug("vaddr %lu is_user_flags %d user_foll_flags %x foll_flags %x.\n",
			vaddr, is_user_flags?1:0, user_foll_flags, foll_flags);
		user_pages = get_user_pages(vaddr & PAGE_MASK, nr_page,
					    foll_flags, pages, NULL);
	}
#endif
	up_read(&current->mm->mmap_sem);
	if (user_pages != nr_page) {
		ret = user_pages < 0 ? user_pages : -ENOMEM;
		pr_err("get_user_pages requested/got: %d/%d]\n", nr_page,
				user_pages);
		while (--user_pages >= 0)
			put_page(pages[user_pages]);
	}
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
#define device_node_from_iter(iter) \
	iter.node
#else
#define device_node_from_iter(iter) \
	iter.out_args.np
#endif

#define CALL_CLEAN_CACHE_ON_INIT 1
#define CALL_FLUSH_CACHE_ON_INIT 2

struct nvmap_chip_cache_op {
	void (*inner_clean_cache_all)(void);
	void (*inner_flush_cache_all)(void);
	void (*nvmap_get_cacheability)(struct nvmap_handle *h,
		bool *inner, bool *outer);
	const char *name;
	int flags;
};

void nvmap_select_cache_ops(struct device *dev);

typedef void (*nvmap_setup_chip_cache_fn)(struct nvmap_chip_cache_op *);

extern struct of_device_id __nvmapcache_of_table;

#define NVMAP_CACHE_OF_DECLARE(compat, fn) \
	_OF_DECLARE(nvmapcache, nvmapcache_of, compat, fn, \
			nvmap_setup_chip_cache_fn)

#endif /* __VIDEO_TEGRA_NVMAP_NVMAP_H */
