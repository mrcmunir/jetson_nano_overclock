/*
 * Copyright (C) 2014-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Hypervisor interfaces
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of NVIDIA CORPORATION nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NVIDIA CORPORATION OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __VMM_SYSCALLS_H__
#define __VMM_SYSCALLS_H__

#include <soc/tegra/virt/tegra_hv_sysmgr.h>

#define HVC_NR_READ_STAT		1
#define HVC_NR_READ_IVC			2
#define HVC_NR_READ_GID			3
#define HVC_NR_RAISE_IRQ		4
#define HVC_NR_READ_NGUESTS		5
#define HVC_NR_READ_IPA_PA		6
#define HVC_NR_READ_GUEST_STATE		7
#define HVC_NR_READ_HYP_INFO		9
#define HVC_NR_GUEST_RESET		10
#define HVC_NR_SYSINFO_IPA		13
#define HVC_NR_ERRINFO_GET		17
#define HVC_NR_ASYNC_ERR_GUEST_READ_ACK	18
#define HVC_NR_READ_VCPU_ID		19
#define HVC_NR_SYNC_ERR_GUEST_READ_ACK	20

#define HVC_NR_TRACE_GET_EVENT_MASK	289
#define HVC_NR_TRACE_SET_EVENT_MASK	290
#define HVC_NR_UART_RELAY_INFO		518
#define HVC_NR_NVLOG_WRITER_INFO	519
#define HVC_NR_NVLOG_READER_INFO	520

#define GUEST_PRIMARY		0
#define GUEST_IVC_SERVER	0

#define NGUESTS_MAX 16

#ifndef __ASSEMBLY__

#if defined(__KERNEL__)
#include <linux/types.h>
#endif

struct tegra_hv_queue_data {
	uint32_t	id;	/* IVC id */
	uint32_t	peers[2];
	uint32_t	size;
	uint32_t	nframes;
	uint32_t	frame_size;
	uint32_t	offset;
	uint16_t	irq, raise_irq;
};

struct ivc_mempool {
	uint64_t pa;
	uint64_t size;
	uint32_t id;
	uint32_t peer_vmid;
};

struct ivc_shared_area {
	uint64_t pa;
	uint64_t size;
	uint32_t guest;
	uint16_t free_irq_start;
	uint16_t free_irq_count;
};

struct ivc_info_page {
	uint32_t nr_queues;
	uint32_t nr_areas;
	uint32_t nr_mempools;

	/* The actual length of this array is nr_areas. */
	struct ivc_shared_area areas[];

	/*
	 * Following the shared array is an array of queue data structures with
	 * an entry per queue that is assigned to the guest. This  array is
	 * terminated by an entry with no frames.
	 *
	 * struct tegra_hv_queue_data queue_data[nr_queues];
	 */

	/*
	 * Following the queue data array is an array of mempool structures
	 * with an entry per mempool assigned to the guest.
	 *
	 * struct ivc_mempool[nr_mempools];
	 */
};

static inline struct ivc_shared_area *ivc_shared_area_addr(
		const struct ivc_info_page *info, uint32_t area_num)
{
	return ((struct ivc_shared_area *) (((uintptr_t) info) + sizeof(*info)))
		+ area_num;
}

static inline const struct tegra_hv_queue_data *ivc_info_queue_array(
		const struct ivc_info_page *info)
{
	return (struct tegra_hv_queue_data *)&info->areas[info->nr_areas];
}

static inline const struct ivc_mempool *ivc_info_mempool_array(
		const struct ivc_info_page *info)
{
	return (struct ivc_mempool *)
			&ivc_info_queue_array(info)[info->nr_queues];
}

struct hyp_ipa_pa_info {
	uint64_t base;       /* base of contiguous pa region */
	uint64_t offset;     /* offset for requested ipa address */
	uint64_t size;       /* size of pa region */
};

#define HVC_MAX_VCPU 64

struct trapped_access {
	uint64_t ipa;
	uint32_t size;
	int32_t write_not_read;
	uint64_t data;
	uint32_t guest_id;
};

struct hyp_server_page {
	/* guest reset protocol */
	uint32_t guest_reset_virq;
	/* boot delay offsets per VM needed by monitor partition */
	uint32_t boot_delay[NGUESTS_MAX];

	uint32_t trap_virq;

	/*
	 * Bitmap of VCPU indices in vcpu_trapped_accesses containing active
	 * trap information.
	 */
	uint32_t trapped_vcpus[HVC_MAX_VCPU / 32];
	struct trapped_access vcpu_trapped_accesses[HVC_MAX_VCPU];

	/* hypervisor trace log */
	uint64_t log_ipa;
	uint32_t log_size;

	/* PCT location Shared with guests */
	uint64_t pct_ipa;

	/* PCT Size Shared with guests in bytes */
	uint64_t pct_size;

};

/* For backwards compatibility, alias the old name for hyp_server_name. */
#define hyp_info_page hyp_server_page

#ifdef CONFIG_ARM64

#define _X3_X17 "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#define _X4_X17 "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#define _X5_X17 "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#define _X6_X17 "x6", "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#define _X7_X17 "x7", "x8", "x9", "x10", "x11", "x12", \
"x13", "x14", "x15", "x16", "x17"

#if IS_ENABLED(CONFIG_KASAN)
#   define __INLINE __no_sanitize_address __maybe_unused
#else
#   define __INLINE inline
#endif

static __INLINE int hyp_read_gid(unsigned int *gid)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_GID)
		: "x2", "x3", _X4_X17);

	*gid = r1;
	return (int)r0;
}

static __INLINE uint32_t hyp_read_vcpu_id(void)
{
	register uint64_t r0 asm("x0");

	asm("hvc %1"
		: "=r"(r0)
		: "i"(HVC_NR_READ_VCPU_ID)
		: "x1", "x2", "x3", _X4_X17);

	return (uint32_t)r0;
}

static __INLINE int hyp_read_nguests(unsigned int *nguests)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_NGUESTS)
		: "x2", "x3", _X4_X17);

	*nguests = r1;
	return (int)r0;
}

static __INLINE int hyp_read_ivc_info(uint64_t *ivc_info_page_pa)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_IVC)
		: "x2", "x3", _X4_X17);

	*ivc_info_page_pa = r1;
	return (int)r0;
}

static __INLINE int hyp_read_ipa_pa_info(struct hyp_ipa_pa_info *info,
		unsigned int guestid, uint64_t ipa)
{
	register uint64_t r0 asm("x0") = guestid;
	register uint64_t r1 asm("x1") = ipa;
	register uint64_t r2 asm("x2");
	register uint64_t r3 asm("x3");


	asm("hvc %4"
		: "+r"(r0), "+r"(r1), "=r"(r2), "=r"(r3)
		: "i"(HVC_NR_READ_IPA_PA)
		: _X4_X17);

	info->base = r1;
	info->offset = r2;
	info->size = r3;

	return (int)r0;
}

static __INLINE int hyp_raise_irq(unsigned int irq, unsigned int vmid)
{
	register uint64_t r0 asm("x0") = irq;
	register uint64_t r1 asm("x1") = vmid;

	asm volatile("hvc %1"
		: "+r"(r0)
		: "i"(HVC_NR_RAISE_IRQ), "r"(r1)
		: "x2", "x3", _X4_X17);

	return (int)r0;
}

static __INLINE int hyp_read_guest_state(unsigned int vmid, unsigned int *state)
{
	register uint64_t r0 asm("x0") = vmid;
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "+r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_GUEST_STATE)
		: "x2", _X3_X17);

	*state = (unsigned int)r1;
	return (int)r0;
}

static __INLINE int hyp_read_hyp_info(uint64_t *hyp_info_page_pa)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");

	asm("hvc %2"
		: "=r"(r0), "=r"(r1)
		: "i"(HVC_NR_READ_HYP_INFO)
		: "x2", "x3", _X4_X17);

	*hyp_info_page_pa = r1;
	return (int)r0;
}

static __INLINE int hyp_guest_reset(unsigned int id,
				  struct hyp_sys_state_info *out)
{
	register uint64_t r0 asm("x0") = id;
	register uint64_t r1 asm("x1");
	register uint64_t r2 asm("x2");
	register uint64_t r3 asm("x3");

	asm volatile("hvc %4"
		: "+r"(r0), "=r"(r1),
		  "=r"(r2), "=r"(r3)
		: "i"(HVC_NR_GUEST_RESET)
		: _X4_X17);

	if (out != 0) {
		out->sys_transition_mask = (uint32_t)r1;
		out->vm_shutdown_mask = (uint32_t)r2;
		out->vm_reboot_mask = (uint32_t)r3;
	}

	return (int)r0;
}

static __INLINE uint64_t hyp_sysinfo_ipa(void)
{
	register uint64_t r0 asm("x0");

	asm("hvc %1"
		: "=r"(r0)
		: "i"(HVC_NR_SYSINFO_IPA)
		: "x1", "x2", "x3", _X4_X17);

	return r0;
}

static __INLINE int hyp_trace_get_mask(uint64_t *mask)
{
	register uint64_t x0 asm("x0");
	register uint64_t x1 asm("x1");

	asm("hvc %[imm16]"
		:
		"=r"(x0), "=r"(x1)
		:
		[imm16] "i"(HVC_NR_TRACE_GET_EVENT_MASK)
		:
		"x2", _X3_X17);

	*mask = x1;

	return (int)x0;
}

static __INLINE int hyp_trace_set_mask(uint64_t mask)
{
	register uint64_t x0 asm("x0") = mask;

	asm volatile ("hvc %[imm16]"
		:
		"+r"(x0)
		:
		[imm16] "i"(HVC_NR_TRACE_SET_EVENT_MASK)
		:
		"x1", "x2", _X3_X17);

	return (int)x0;
}

static __INLINE int hyp_read_uart_relay_info(uint64_t *ipa, uint64_t *size,
					uint64_t *num_channels,
					uint64_t *max_msg_size)
{
	register uint64_t x0 asm("x0");
	register uint64_t x1 asm("x1");
	register uint64_t x2 asm("x2");
	register uint64_t x3 asm("x3");
	register uint64_t x4 asm("x4");

	asm("hvc %5"
		: "=r"(x0), "=r"(x1),
		  "=r"(x2), "=r"(x3),
		  "=r"(x4)
		: "i"(HVC_NR_UART_RELAY_INFO)
		: _X5_X17);

	*ipa = x1;
	*size = x2;
	*num_channels = x3;
	*max_msg_size = x4;

	return (int)x0;
}

static __INLINE int hyp_read_nvlog_reader_info(uint64_t *ipa, uint64_t *size,
					uint64_t *num_vms)
{
	register uint64_t x0 asm("x0");
	register uint64_t x1 asm("x1");
	register uint64_t x2 asm("x2");
	register uint64_t x3 asm("x3");
	register uint64_t x4 asm("x4");

	asm("hvc %5"
		: "=r"(x0), "=r"(x1),
		  "=r"(x2), "=r"(x3),
		  "=r"(x4)
		: "i"(HVC_NR_NVLOG_READER_INFO)
		: _X5_X17);

	*ipa = x1;
	*size = x2;
	*num_vms = x3;

	return (int)x0;
}

static __INLINE int hyp_read_nvlog_writer_info(uint64_t *ipa, uint64_t *size)
{
	register uint64_t x0 asm("x0");
	register uint64_t x1 asm("x1");
	register uint64_t x2 asm("x2");
	register uint64_t x3 asm("x3");
	register uint64_t x4 asm("x4");

	asm("hvc %5"
		: "=r"(x0), "=r"(x1),
		  "=r"(x2), "=r"(x3),
		  "=r"(x4)
		: "i"(HVC_NR_NVLOG_WRITER_INFO)
		: _X5_X17);

	*ipa = x1;
	*size = x2;

	return (int)x0;
}

static __INLINE int hyp_read_err_info_get(uint64_t *ipa, uint64_t *buff_size,
	unsigned int *async_err_arr_items, int *peer_err_irq_id,
	unsigned int *vcpu_cnt)
{
	register uint64_t r0 asm("x0");
	register uint64_t r1 asm("x1");
	register uint64_t r2 asm("x2");
	register uint64_t r3 asm("x3");
	register uint64_t r4 asm("x4");
	register uint64_t r5 asm("x5");

	asm volatile("hvc %6"
		: "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3), "=r"(r4), "=r"(r5)
		: "i"(HVC_NR_ERRINFO_GET)
		: _X6_X17);

	*ipa = r1;
	*buff_size = r2;
	*async_err_arr_items = r3;
	*peer_err_irq_id = (int) r4;
	*vcpu_cnt = r5;

	return (int)r0;
}

static __INLINE int hyp_send_async_err_ack(uint64_t local_rd_idx)
{
	register uint64_t r0 asm("x0") = local_rd_idx;

	asm volatile("hvc %1"
		: "+r"(r0)
		: "i"(HVC_NR_ASYNC_ERR_GUEST_READ_ACK)
		: "x1", "x2", "x3", _X4_X17);

	return (int)r0;
}

static __INLINE int hyp_send_sync_err_ack(void)
{
	register uint64_t r0 asm("x0");

	asm volatile("hvc %1"
		: "=r"(r0)
		: "i"(HVC_NR_SYNC_ERR_GUEST_READ_ACK)
		: "x1", "x2", "x3", _X4_X17);

	return (int)r0;
}

#undef _X3_X17
#undef _X4_X17
#undef _X5_X17
#undef _X6_X17
#undef _X7_X17

#else

int hyp_read_gid(unsigned int *gid);
uint32_t hyp_read_vcpu_id(void);
int hyp_read_nguests(unsigned int *nguests);
int hyp_read_ivc_info(uint64_t *ivc_info_page_pa);
int hyp_read_ipa_pa_info(struct hyp_ipa_pa_info *info, int guestid,
		uint64_t ipa);
int hyp_raise_irq(unsigned int irq, unsigned int vmid);
uint64_t hyp_sysinfo_ipa(void);
int hyp_read_err_info_get(uint64_t *ipa, uint64_t *buff_size,
	unsigned int *async_err_arr_size, int *peer_err_irq_id,
	uint64_t *sync_err_offset, unsigned int  *vcpu_cnt);
int hyp_send_async_err_ack(uint64_t local_rd_idx);
int hyp_send_sync_err_ack(void);

/* ASM prototypes */
extern int hvc_read_gid(void *);
extern int hvc_read_ivc_info(int *);
extern int hvc_read_ipa_pa_info(void *, int guestid, uint64_t ipa);
extern int hvc_read_nguests(void *);
extern int hvc_raise_irq(unsigned int irq, unsigned int vmid);

#endif /* CONFIG_ARCH_ARM64 */

#endif /* !__ASSEMBLY__ */

#endif /* __VMM_SYSCALLS_H__ */
