/*
 * This header provides macros for MAXIM MAX8973 device bindings.
 *
 * Copyright (c) 2013-2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DT_BINDINGS_REGULATOR_MAX8973_H__
#define __DT_BINDINGS_REGULATOR_MAX8973_H__

/*
 * Control flags for configuration of the device.
 * Client need to pass this information with ORed
 */
#define MAX8973_CONTROL_REMOTE_SENSE_ENABLE			0x00000001
#define MAX8973_CONTROL_FALLING_SLEW_RATE_ENABLE		0x00000002
#define MAX8973_CONTROL_OUTPUT_ACTIVE_DISCH_ENABLE		0x00000004
#define MAX8973_CONTROL_BIAS_ENABLE				0x00000008
#define MAX8973_CONTROL_PULL_DOWN_ENABLE			0x00000010
#define MAX8973_CONTROL_FREQ_SHIFT_9PER_ENABLE			0x00000020

#define MAX8973_CONTROL_CLKADV_TRIP_DISABLED			0x00000000
#define MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US			0x00010000
#define MAX8973_CONTROL_CLKADV_TRIP_150mV_PER_US		0x00020000
#define MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US_HIST_DIS	0x00030000

#define MAX8973_CONTROL_INDUCTOR_VALUE_NOMINAL			0x00000000
#define MAX8973_CONTROL_INDUCTOR_VALUE_MINUS_30_PER		0x00100000
#define MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_30_PER		0x00200000
#define MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_60_PER		0x00300000

#endif /* __DT_BINDINGS_REGULATOR_MAX8973_H__ */
