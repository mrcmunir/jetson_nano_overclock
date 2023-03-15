/*
 * functions and macros to control the flowcontroller
 *
 * Copyright (c) 2010-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MACH_TEGRA_FLOWCTRL_H
#define __MACH_TEGRA_FLOWCTRL_H

#define FLOW_CTRL_HALT_CPU0_EVENTS	0x0
#define FLOW_CTRL_WAITEVENT		(2 << 29)
#define FLOW_CTRL_WAIT_FOR_INTERRUPT	(4 << 29)
#define FLOW_CTRL_JTAG_RESUME		(1 << 28)
#define FLOW_CTRL_HALT_CPU_IRQ		(1 << 10)
#define	FLOW_CTRL_HALT_CPU_FIQ		(1 << 8)
#define FLOW_CTRL_CPU0_CSR		0x8
#define	FLOW_CTRL_CSR_INTR_FLAG		(1 << 15)
#define FLOW_CTRL_CSR_EVENT_FLAG	(1 << 14)
#define FLOW_CTRL_CSR_ENABLE		(1 << 0)
#define FLOW_CTRL_HALT_CPU1_EVENTS	0x14
#define FLOW_CTRL_CPU1_CSR		0x18
#define FLOW_CTLR_CC4_HVC_CONTROL	0x60
#define FLOW_CTRL_CC4_HVC_ENABLE	(1 << 0)
#define FLOW_CTRL_CC4_RETENTION_CONTROL	0x64
#define FLOW_CTRL_CC4_CORE0_CTRL	0x6c
#define FLOW_CTRL_WAIT_WFI_BITMAP	0x100
#define FLOW_CTRL_CC4_HVC_RETRY		0x8c

#define TEGRA20_FLOW_CTRL_CSR_WFE_CPU0         (1 << 4)
#define TEGRA20_FLOW_CTRL_CSR_WFE_BITMAP       (3 << 4)
#define TEGRA20_FLOW_CTRL_CSR_WFI_BITMAP       0

#define TEGRA30_FLOW_CTRL_CSR_WFI_CPU0         (1 << 8)
#define TEGRA30_FLOW_CTRL_CSR_WFE_BITMAP       (0xF << 4)
#define TEGRA30_FLOW_CTRL_CSR_WFI_BITMAP       (0xF << 8)

#ifndef __ASSEMBLY__
void flowctrl_update(u8 offset, u32 value);
void flowctrl_write_cc4_ctrl(unsigned int cpuid, u32 value);
void flowctrl_write_cpu_csr(unsigned int cpuid, u32 value);
void flowctrl_write_cpu_halt(unsigned int cpuid, u32 value);
#endif

#endif
