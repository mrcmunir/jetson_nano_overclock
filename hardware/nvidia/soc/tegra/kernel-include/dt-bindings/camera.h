/*
 * Copyright (c) 2016-2020, NVIDIA CORPORATION.  All rights reserved.
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

/*
 * This header provides constants for binding nvidia,camera.
 *
 */

#ifndef __DT_BINDINGS_MEDIA_CAMERA_H__
#define __DT_BINDINGS_MEDIA_CAMERA_H__

#define NV_TRUE			1
#define	NV_FALSE		0

#define CAMERA_INT_MASK			0xf0000000
#define CAMERA_TABLE_WAIT_US		(CAMERA_INT_MASK | 1)
#define CAMERA_TABLE_WAIT_MS		(CAMERA_INT_MASK | 2)
#define CAMERA_TABLE_END		(CAMERA_INT_MASK | 9)
#define CAMERA_TABLE_PWR		(CAMERA_INT_MASK | 20)
#define CAMERA_TABLE_PINMUX		(CAMERA_INT_MASK | 25)
#define CAMERA_TABLE_INX_PINMUX		(CAMERA_INT_MASK | 26)
#define CAMERA_TABLE_GPIO_ACT		(CAMERA_INT_MASK | 30)
#define CAMERA_TABLE_GPIO_DEACT		(CAMERA_INT_MASK | 31)
#define CAMERA_TABLE_GPIO_INX_ACT	(CAMERA_INT_MASK | 32)
#define CAMERA_TABLE_GPIO_INX_DEACT	(CAMERA_INT_MASK | 33)
#define CAMERA_TABLE_REG_NEW_POWER	(CAMERA_INT_MASK | 40)
#define CAMERA_TABLE_INX_POWER		(CAMERA_INT_MASK | 41)
#define CAMERA_TABLE_INX_CLOCK		(CAMERA_INT_MASK | 50)
#define CAMERA_TABLE_INX_CGATE		(CAMERA_INT_MASK | 51)
#define CAMERA_TABLE_EDP_STATE		(CAMERA_INT_MASK | 60)

#define CAMERA_TABLE_PWR_FLAG_MASK	0xf0000000
#define CAMERA_TABLE_PWR_FLAG_ON	0x80000000
#define CAMERA_TABLE_PINMUX_FLAG_MASK	0xf0000000
#define CAMERA_TABLE_PINMUX_FLAG_ON	0x80000000
#define CAMERA_TABLE_CLOCK_VALUE_BITS	24
#define CAMERA_TABLE_CLOCK_VALUE_MASK	\
			((u32)(-1) >> (32 - CAMERA_TABLE_CLOCK_VALUE_BITS))
#define CAMERA_TABLE_CLOCK_INDEX_BITS	(32 - CAMERA_TABLE_CLOCK_VALUE_BITS)
#define CAMERA_TABLE_CLOCK_INDEX_MASK	\
			((u32)(-1) << (32 - CAMERA_TABLE_CLOCK_INDEX_BITS))

#define CAMERA_PWR_ON(x)	(CAMERA_TABLE_PWR_FLAG_ON + x)
#define CAMERA_PWR_OFF(x)	x

#define CAMERA_MAX_EDP_ENTRIES  16
#define CAMERA_MAX_NAME_LENGTH	32
#define CAMDEV_INVALID		0xffffffff

#define	CAMERA_SEQ_STATUS_MASK	0xf0000000
#define	CAMERA_SEQ_INDEX_MASK	0x0000ffff
#define	CAMERA_SEQ_FLAG_MASK	(~CAMERA_SEQ_INDEX_MASK)
#define	CAMERA_SEQ_FLAG_EDP	0x80000000

#define CAMERA_IND_CLK_SET(x)	CAMERA_TABLE_INX_CLOCK x
#define CAMERA_IND_CLK_CLR	CAMERA_TABLE_INX_CLOCK 0
#define CAMERA_GPIO_SET(x)	CAMERA_TABLE_GPIO_ACT x
#define CAMERA_GPIO_CLR(x)	CAMERA_TABLE_GPIO_DEACT x
#define CAMERA_REGULATOR_ON(x)	CAMERA_TABLE_PWR CAMERA_PWR_ON(x)
#define CAMERA_REGULATOR_OFF(x)	CAMERA_TABLE_PWR CAMERA_PWR_OFF(x)
#define CAMERA_WAITMS(x)	CAMERA_TABLE_WAIT_MS x
#define CAMERA_WAITUS(x)	CAMERA_TABLE_WAIT_US x
#define CAMERA_END		CAMERA_TABLE_END 0

#endif
/* __DT_BINDINGS_MEDIA_CAMERA_H__ */
