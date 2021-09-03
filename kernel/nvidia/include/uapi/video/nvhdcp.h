/*
 * include/uapi/video/nvhdcp.h
 *
 * nvhdcp.h: tegra dc hdcp declarations.
 *
 * Copyright (c) 2016-2020, NVIDIA CORPORATION, All rights reserved.
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

#ifndef __UAPI_LINUX_NVHDCP_H_
#define __UAPI_LINUX_NVHDCP_H_

#include <linux/fb.h>
#include <linux/types.h>
#include <asm-generic/ioctl.h>

/* maximum receivers and repeaters connected at a time */
#define TEGRA_NVHDCP_MAX_DEVS	127

/* values for value_flags */
#define TEGRA_NVHDCP_FLAG_AN			0x0001
#define TEGRA_NVHDCP_FLAG_AKSV			0x0002
#define TEGRA_NVHDCP_FLAG_BKSV			0x0004
#define TEGRA_NVHDCP_FLAG_BSTATUS		0x0008 /* repeater status */
#define TEGRA_NVHDCP_FLAG_CN			0x0010 /* c_n */
#define TEGRA_NVHDCP_FLAG_CKSV			0x0020 /* c_ksv */
#define TEGRA_NVHDCP_FLAG_DKSV			0x0040 /* d_ksv */
#define TEGRA_NVHDCP_FLAG_KP			0x0080 /* k_prime */
#define TEGRA_NVHDCP_FLAG_S			0x0100 /* hdcp_status */
#define TEGRA_NVHDCP_FLAG_CS			0x0200 /* connection state */
#define TEGRA_NVHDCP_FLAG_V			0x0400
#define TEGRA_NVHDCP_FLAG_MP			0x0800
#define TEGRA_NVHDCP_FLAG_BKSVLIST		0x1000

/* values for packet_results */
#define TEGRA_NVHDCP_RESULT_SUCCESS		0
#define TEGRA_NVHDCP_RESULT_UNSUCCESSFUL	1
#define TEGRA_NVHDCP_RESULT_PENDING		0x103
#define TEGRA_NVHDCP_RESULT_LINK_FAILED		0xc0000013
/* TODO: replace with -EINVAL */
#define TEGRA_NVHDCP_RESULT_INVALID_PARAMETER	0xc000000d
#define TEGRA_NVHDCP_RESULT_INVALID_PARAMETER_MIX	0xc0000030
/* TODO: replace with -ENOMEM */
#define TEGRA_NVHDCP_RESULT_NO_MEMORY		0xc0000017

struct tegra_nvhdcp_packet {
	__u32	value_flags;		// (IN/OUT)
	__u32	packet_results;		// (OUT)

	__u64	c_n;			// (IN) upstream exchange number
	__u64	c_ksv;			// (IN)

	__u32	b_status;	// (OUT) link/repeater status for HDMI
	__u64	hdcp_status;	// (OUT) READ_S
	__u64	cs;		// (OUT) Connection State

	__u64	k_prime;	// (OUT)
	__u64	a_n;		// (OUT)
	__u64	a_ksv;		// (OUT)
	__u64	b_ksv;		// (OUT)
	__u64	d_ksv;		// (OUT)
	__u8	v_prime[20];	// (OUT) 160-bit
	__u64	m_prime;	// (OUT)

	// (OUT) Valid KSVs in the bKsvList. Maximum is 127 devices
	__u32	num_bksv_list;

	// (OUT) Up to 127 receivers & repeaters
	__u64	bksv_list[TEGRA_NVHDCP_MAX_DEVS];

	__u32	hdcp22;

	__u32 port; /* (OUT) DP or HDMI */

	__u32 binfo; /* (OUT) link/repeater status for DP */

	__u32 sor; /* (OUT) SOR or SOR1 */
};

/* parameters to TEGRAIO_NVHDCP_SET_POLICY */
#define TEGRA_NVHDCP_POLICY_ON_DEMAND	0
#define TEGRA_NVHDCP_POLICY_ALWAYS_ON	1
#define TEGRA_NVHDCP_POLICY_ALWAYS_OFF	2

/* ioctls */
#define TEGRAIO_NVHDCP_ON		_IO('F', 0x70)
#define TEGRAIO_NVHDCP_OFF		_IO('F', 0x71)
#define TEGRAIO_NVHDCP_SET_POLICY	_IOW('F', 0x72, __u32)
#define TEGRAIO_NVHDCP_READ_M		_IOWR('F', 0x73, struct tegra_nvhdcp_packet)
#define TEGRAIO_NVHDCP_READ_S		_IOWR('F', 0x74, struct tegra_nvhdcp_packet)
#define TEGRAIO_NVHDCP_RENEGOTIATE	_IO('F', 0x75)
#define TEGRAIO_NVHDCP_HDCP_STATE	_IOR('F', 0x76, struct tegra_nvhdcp_packet)
#define TEGRAIO_NVHDCP_RECV_CAPABLE	_IOR('F', 0x77, __u32)

/* distinguish between HDMI and DP ports */
#define TEGRA_NVHDCP_PORT_DP	2
#define TEGRA_NVHDCP_PORT_HDMI	3

#endif
