/*
 * drivers/misc/tegra-cec/tegra_cec.h
 *
 * Copyright (c) 2012-2020, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef TEGRA_CEC_H
#define TEGRA_CEC_H

#include <linux/pm.h>
#include <asm/atomic.h>
#include <uapi/misc/tegra_cec.h>

#define TEGRA_CEC_FRAME_MAX_LENGTH  16

struct tegra_cec_soc;

struct tegra_cec {
	struct device		*dev;
	struct miscdevice 	misc_dev;
	struct clk		*clk;
	struct mutex		tx_lock;
	struct mutex		recovery_lock;
	void __iomem		*cec_base;
	int			tegra_cec_irq;
	wait_queue_head_t	rx_waitq;
	wait_queue_head_t	tx_waitq;
	wait_queue_head_t	init_waitq;
	atomic_t		init_done;
#ifdef CONFIG_PM
	wait_queue_head_t	suspend_waitq;
	atomic_t		init_cancel;
#endif
	u16			logical_addr;
	struct work_struct	work;
	const struct tegra_cec_soc *soc;
	unsigned int		rx_wake;
	unsigned int		tx_wake;
	u16			rx_buffer;
	long			tx_error;
	u32			tx_buf[TEGRA_CEC_FRAME_MAX_LENGTH];
	u8			tx_buf_cur;
	u8			tx_buf_cnt;
};
static int tegra_cec_remove(struct platform_device *pdev);

#define TEGRA_CEC_LADDR_BROADCAST   0xF
#define TEGRA_CEC_LADDR_MASK        0xF
#define TEGRA_CEC_LADDR_WIDTH       4
#define TEGRA_CEC_LADDR_MODE(blk) \
	((blk & TEGRA_CEC_LADDR_MASK) == TEGRA_CEC_LADDR_BROADCAST)

/*CEC Timing registers*/
#define TEGRA_CEC_SW_CONTROL 	 0X000
#define TEGRA_CEC_HW_CONTROL	 0X004
#define TEGRA_CEC_INPUT_FILTER	 0X008
#define TEGRA_CEC_TX_REGISTER	 0X010
#define TEGRA_CEC_RX_REGISTER	 0X014
#define TEGRA_CEC_RX_TIMING_0	 0X018
#define TEGRA_CEC_RX_TIMING_1	 0X01C
#define TEGRA_CEC_RX_TIMING_2	 0X020
#define TEGRA_CEC_TX_TIMING_0	 0X024
#define TEGRA_CEC_TX_TIMING_1	 0X028
#define TEGRA_CEC_TX_TIMING_2	 0X02C
#define TEGRA_CEC_INT_STAT	 0X030
#define TEGRA_CEC_INT_MASK	 0X034
#define TEGRA_CEC_HW_DEBUG_RX	 0X038
#define TEGRA_CEC_HW_DEBUG_TX	 0X03C
#define TEGRA_CEC_HW_SPARE       0X040

#define TEGRA_CEC_MAX_LOGICAL_ADDR	15
#define TEGRA_CEC_HWCTRL_RX_LADDR_UNREG	0x0
#define TEGRA_CEC_HWCTRL_RX_LADDR_MASK	0x7FFF
#define TEGRA_CEC_HWCTRL_RX_LADDR(x)	\
	((x<<0) & TEGRA_CEC_HWCTRL_RX_LADDR_MASK)
#define TEGRA_CEC_HWCTRL_RX_SNOOP	(1<<15)
#define TEGRA_CEC_HWCTRL_RX_NAK_MODE	(1<<16)
#define TEGRA_CEC_HWCTRL_TX_NAK_MODE	(1<<24)
#define TEGRA_CEC_HWCTRL_FAST_SIM_MODE	(1<<30)
#define TEGRA_CEC_HWCTRL_TX_RX_MODE	(1<<31)

#define TEGRA_CEC_INPUT_FILTER_MODE	(1<<31)
#define TEGRA_CEC_INPUT_FILTER_FIFO_LENGTH_MASK	0

#define TEGRA_CEC_TX_REG_DATA_SHIFT		0
#define TEGRA_CEC_TX_REG_EOM_SHIFT		8
#define TEGRA_CEC_TX_REG_ADDR_MODE_SHIFT	12
#define TEGRA_CEC_TX_REG_START_BIT_SHIFT	16
#define TEGRA_CEC_TX_REG_RETRY_BIT_SHIFT	17

#define TEGRA_CEC_RX_REGISTER_MASK	 0
#define TEGRA_CEC_RX_REGISTER_EOM	 (1<<8)
#define TEGRA_CEC_RX_REGISTER_ACK	 (1<<9)

#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MAX_LO_TIME_MASK	 0
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MIN_LO_TIME_MASK	 8
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MAX_DURATION_MASK	 16
#define TEGRA_CEC_RX_TIMING_0_RX_START_BIT_MIN_DURATION_MASK	 24

#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MAX_LO_TIME_MASK	 0
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_SAMPLE_TIME_MASK	 8
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MAX_DURATION_MASK	 16
#define TEGRA_CEC_RX_TIMING_1_RX_DATA_BIT_MIN_DURATION_MASK	 24

#define TEGRA_CEC_RX_TIMING_2_RX_END_OF_BLOCK_TIME_MASK	 0

#define TEGRA_CEC_TX_TIMING_0_TX_START_BIT_LO_TIME_MASK	 	0
#define TEGRA_CEC_TX_TIMING_0_TX_START_BIT_DURATION_MASK	8
#define TEGRA_CEC_TX_TIMING_0_TX_BUS_XITION_TIME_MASK	 	16
#define TEGRA_CEC_TX_TIMING_0_TX_BUS_ERROR_LO_TIME_MASK	 	24

#define TEGRA_CEC_TX_TIMING_1_TX_LO_DATA_BIT_LO_TIME_MASK	0
#define TEGRA_CEC_TX_TIMING_1_TX_HI_DATA_BIT_LO_TIME_MASK	8
#define TEGRA_CEC_TX_TIMING_1_TX_DATA_BIT_DURATION_MASK	 	16
#define TEGRA_CEC_TX_TIMING_1_TX_ACK_NAK_BIT_SAMPLE_TIME_MASK	24

#define TEGRA_CEC_TX_TIMING_2_BUS_IDLE_TIME_ADDITIONAL_FRAME_MASK	0
#define TEGRA_CEC_TX_TIMING_2_BUS_IDLE_TIME_NEW_FRAME_MASK		4
#define TEGRA_CEC_TX_TIMING_2_BUS_IDLE_TIME_RETRY_FRAME_MASK		8

#define TEGRA_CEC_INT_STAT_TX_REGISTER_EMPTY	 		(1<<0)
#define TEGRA_CEC_INT_STAT_TX_REGISTER_UNDERRUN	 		(1<<1)
#define TEGRA_CEC_INT_STAT_TX_FRAME_OR_BLOCK_NAKD		(1<<2)
#define TEGRA_CEC_INT_STAT_TX_ARBITRATION_FAILED		(1<<3)
#define TEGRA_CEC_INT_STAT_TX_BUS_ANOMALY_DETECTED		(1<<4)
#define TEGRA_CEC_INT_STAT_TX_FRAME_TRANSMITTED			(1<<5)
#define TEGRA_CEC_INT_STAT_RX_REGISTER_FULL			(1<<8)
#define TEGRA_CEC_INT_STAT_RX_REGISTER_OVERRUN			(1<<9)
#define TEGRA_CEC_INT_STAT_RX_START_BIT_DETECTED		(1<<10)
#define TEGRA_CEC_INT_STAT_RX_BUS_ANOMALY_DETECTED		(1<<11)
#define TEGRA_CEC_INT_STAT_RX_BUS_ERROR_DETECTED		(1<<12)
#define TEGRA_CEC_INT_STAT_FILTERED_RX_DATA_PIN_TRANSITION_H2L	(1<<13)
#define TEGRA_CEC_INT_STAT_FILTERED_RX_DATA_PIN_TRANSITION_L2H	(1<<14)

#define TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY	 		(1<<0)
#define TEGRA_CEC_INT_MASK_TX_REGISTER_UNDERRUN	 		(1<<1)
#define TEGRA_CEC_INT_MASK_TX_FRAME_OR_BLOCK_NAKD		(1<<2)
#define TEGRA_CEC_INT_MASK_TX_ARBITRATION_FAILED		(1<<3)
#define TEGRA_CEC_INT_MASK_TX_BUS_ANOMALY_DETECTED		(1<<4)
#define TEGRA_CEC_INT_MASK_TX_FRAME_TRANSMITTED			(1<<5)
#define TEGRA_CEC_INT_MASK_RX_REGISTER_FULL			(1<<8)
#define TEGRA_CEC_INT_MASK_RX_REGISTER_OVERRUN			(1<<9)
#define TEGRA_CEC_INT_MASK_RX_START_BIT_DETECTED		(1<<10)
#define TEGRA_CEC_INT_MASK_RX_BUS_ANOMALY_DETECTED		(1<<11)
#define TEGRA_CEC_INT_MASK_RX_BUS_ERROR_DETECTED		(1<<12)
#define TEGRA_CEC_INT_MASK_FILTERED_RX_DATA_PIN_TRANSITION_H2L	(1<<13)
#define TEGRA_CEC_INT_MASK_FILTERED_RX_DATA_PIN_TRANSITION_L2H	(1<<14)

#define TEGRA_CEC_HW_DEBUG_TX_DURATION_COUNT_MASK	0
#define TEGRA_CEC_HW_DEBUG_TX_TXBIT_COUNT_MASK		17
#define TEGRA_CEC_HW_DEBUG_TX_STATE_MASK		21
#define TEGRA_CEC_HW_DEBUG_TX_FORCELOOUT		(1<<25)
#define TEGRA_CEC_HW_DEBUG_TX_TXDATABIT_SAMPLE_TIMER	(1<<26)

#define TEGRA_CEC_NAME "tegra_cec"

#endif /* TEGRA_CEC_H */
