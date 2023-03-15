/*
 * include/trace/events/dmadebug.h
 *
 * DMA debugging event logging to ftrace.
 *
 * Copyright (c) 2013-2017, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmadebug

#if !defined(_TRACE_DMADEBUG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DMADEBUG_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/dma-debug.h>
#include <asm/io.h>

DECLARE_EVENT_CLASS(dmadebug,
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size, \
		struct page *page),

	TP_ARGS(dev, dma_addr, size, page),

	TP_STRUCT__entry(
		__field(struct device *, dev)
		__string(name, dev_name(dev))
		__field(dma_addr_t, dma_addr)
		__field(size_t, size)
		__field(struct page *, page)
	),

	TP_fast_assign(
		__entry->dev = dev;
		__assign_str(name, dev_name(dev));
		__entry->dma_addr = dma_addr;
		__entry->size = size;
		__entry->page = page;
	),

	TP_printk("device=%s, iova=%pad, size=%zu phys=%llx platformdata=%s",
		   __get_str(name), &__entry->dma_addr,
		   __entry->size,
		   (unsigned long long)page_to_phys(__entry->page),
		   debug_dma_platformdata(__entry->dev))
);

#define DMADEBUGEVENT(ev) DEFINE_EVENT(dmadebug, ev, \
	TP_PROTO(struct device *dev, dma_addr_t dma_addr, size_t size, \
		struct page *page), \
	TP_ARGS(dev, dma_addr, size, page) \
)

DMADEBUGEVENT(dmadebug_alloc_attrs);
DMADEBUGEVENT(dmadebug_free_attrs);
DMADEBUGEVENT(dmadebug_map_page);
DMADEBUGEVENT(dmadebug_unmap_page);
DMADEBUGEVENT(dmadebug_map_sg);
DMADEBUGEVENT(dmadebug_unmap_sg);

#undef DMADEBUGEVENT

#endif /*  _TRACE_DMADEBUG_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
