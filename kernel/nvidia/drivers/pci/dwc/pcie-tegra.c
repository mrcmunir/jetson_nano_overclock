/*
 * Copyright (c) 2017 - 2021, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/platform_device.h>
#include <linux/platform/tegra/emc_bwmgr.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/resource.h>
#include <soc/tegra/chip-id.h>
#include <soc/tegra/bpmp_abi.h>
#include <soc/tegra/tegra_bpmp.h>
#include <linux/random.h>
#include <uapi/linux/pci_regs.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>

#include "pcie-designware.h"

#define dw_pcie_to_tegra_pcie(x) container_of(x, struct tegra_pcie_dw, pci)

#define CTRL_0	(0)
#define CTRL_1	(1)
#define CTRL_2	(2)
#define CTRL_3	(3)
#define CTRL_4	(4)
#define CTRL_5	(5)

#define APPL_PINMUX				(0X0)
#define APPL_PINMUX_PEX_RST			BIT(0)
#define APPL_PINMUX_CLKREQ_OVERRIDE_EN		BIT(2)
#define APPL_PINMUX_CLKREQ_OVERRIDE		BIT(3)
#define APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN	BIT(4)
#define APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE	BIT(5)
#define APPL_PINMUX_PEX_RST_IN_OVERRIDE_EN	BIT(11)

#define APPL_CTRL				(0X4)
#define APPL_CTRL_HW_HOT_RST_MODE_MASK		(0X3)
#define APPL_CTRL_HW_HOT_RST_MODE_SHIFT		22
#define APPL_CTRL_HW_HOT_RST_MODE_DLY_RST	0x0
#define APPL_CTRL_HW_HOT_RST_MODE_IMDT_RST	0x1
#define APPL_CTRL_SYS_PRE_DET_STATE		BIT(6)
#define APPL_CTRL_LTSSM_EN			BIT(7)
#define APPL_CTRL_HW_HOT_RST_EN			BIT(20)

#define APPL_INTR_EN_L0_0			0x8
#define APPL_INTR_EN_L0_0_SYS_MSI_INTR_EN	BIT(31)
#define APPL_INTR_EN_L0_0_SYS_INTR_EN		BIT(30)
#define APPL_INTR_EN_L0_0_CDM_REG_CHK_INT_EN	BIT(19)
#define APPL_INTR_EN_L0_0_AXI_APB_ERR_INT_EN	BIT(17)
#define APPL_INTR_EN_L0_0_PEX_RST_INT_EN	BIT(16)
#define APPL_INTR_EN_L0_0_PCI_CMD_EN_INT_EN	BIT(15)
#define APPL_INTR_EN_L0_0_CPL_TIMEOUT_INT_EN	BIT(13)
#define APPL_INTR_EN_L0_0_INT_INT_EN		BIT(8)
#define APPL_INTR_EN_L0_0_MSI_RCV_INT_EN	BIT(4)
#define APPL_INTR_EN_L0_0_ERROR_INT_EN		BIT(1)
#define APPL_INTR_EN_L0_0_LINK_STATE_INT_EN	BIT(0)

#define APPL_INTR_STATUS_L0			0xC
#define APPL_INTR_STATUS_L0_CDM_REG_CHK_INT	BIT(18)
#define APPL_INTR_STATUS_L0_PEX_RST_INT		BIT(16)
#define APPL_INTR_STATUS_L0_PCI_CMD_EN_INT	BIT(15)
#define APPL_INTR_STATUS_L0_INT_INT		BIT(8)
#define APPL_INTR_STATUS_L0_LINK_STATE_INT	BIT(0)

#define APPL_INTR_EN_L1_0_0				0x1C
#define APPL_INTR_EN_L1_0_0_LINK_REQ_RST_NOT_INT_EN	BIT(1)
#define APPL_INTR_EN_L1_0_0_RDLH_LINK_UP_INT_EN		BIT(3)
#define APPL_INTR_EN_L1_0_0_HOT_RESET_DONE_INT_EN	BIT(30)

#define APPL_INTR_STATUS_L1_0_0				0x20
#define APPL_INTR_STATUS_L1_0_0_LINK_REQ_RST_NOT_CHGED	BIT(1)
#define APPL_INTR_STATUS_L1_0_0_RDLH_LINK_UP_CHGED	BIT(3)
#define APPL_INTR_STATUS_L1_0_0_SURPRISE_DOWN_ERR_STATE	BIT(4)
#define APPL_INTR_STATUS_L1_0_0_HOT_RESET_DONE		BIT(30)

#define APPL_INTR_STATUS_L1_1			0x2C
#define APPL_INTR_STATUS_L1_2			0x30
#define APPL_INTR_STATUS_L1_3			0x34
#define APPL_INTR_STATUS_L1_6			0x3C
#define APPL_INTR_STATUS_L1_7			0x40
#define APPL_INTR_STATUS_L1_15_CFG_BME_CHGED	BIT(1)

#define APPL_INTR_EN_L1_8_0			0x44
#define APPL_INTR_EN_L1_8_AER_INT_EN		BIT(15)
#define APPL_INTR_EN_L1_8_INTX_EN		BIT(11)
#define APPL_INTR_EN_L1_8_EDMA_INT_EN		BIT(6)
#define APPL_INTR_EN_L1_8_AUTO_BW_INT_EN	BIT(3)
#define APPL_INTR_EN_L1_8_BW_MGT_INT_EN		BIT(2)

#define APPL_INTR_STATUS_L1_8_0			0x4C
#define APPL_INTR_STATUS_L1_8_0_EDMA_INT_MASK	0xFC0
#define APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS	BIT(3)
#define APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS	BIT(2)

#define APPL_INTR_STATUS_L1_9			0x54
#define APPL_INTR_STATUS_L1_10			0x58
#define APPL_INTR_STATUS_L1_11			0x64
#define APPL_INTR_STATUS_L1_13			0x74
#define APPL_INTR_STATUS_L1_14			0x78
#define APPL_INTR_STATUS_L1_15			0x7C
#define APPL_INTR_STATUS_L1_17			0x88

#define APPL_INTR_EN_L1_18				0x90
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_CMPLT		BIT(2)
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_CMP_ERR		BIT(1)
#define APPL_INTR_EN_L1_18_CDM_REG_CHK_LOGIC_ERR	BIT(0)

#define APPL_INTR_STATUS_L1_18				0x94
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMPLT	BIT(2)
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMP_ERR	BIT(1)
#define APPL_INTR_STATUS_L1_18_CDM_REG_CHK_LOGIC_ERR	BIT(0)

#define APPL_MSI_CTRL_1				0xAC

#define APPL_MSI_CTRL_2				0xB0

#define APPL_LTR_MSG_1				0xC4
#define LTR_MSG_REQ				BIT(15)
#define LTR_MST_NO_SNOOP_SHIFT			16

#define APPL_LTR_MSG_2				0xC8
#define APPL_LTR_MSG_2_LTR_MSG_REQ_STATE	BIT(3)

#define APPL_LINK_STATUS			0xCC
#define APPL_LINK_STATUS_RDLH_LINK_UP		BIT(0)

#define APPL_DEBUG				0xD0
#define APPL_DEBUG_PM_LINKST_IN_L2_LAT		BIT(21)
#define APPL_DEBUG_PM_LINKST_IN_L0		0x11
#define APPL_DEBUG_LTSSM_STATE_MASK		GENMASK(8, 3)
#define APPL_DEBUG_LTSSM_STATE_SHIFT		3
#define LTSSM_STATE_PRE_DETECT			5

#define APPL_RADM_STATUS			0xE4
#define APPL_PM_XMT_TURNOFF_STATE		BIT(0)

#define APPL_DM_TYPE				0x100
#define APPL_DM_TYPE_MASK			0xF
#define APPL_DM_TYPE_RP				0x4
#define APPL_DM_TYPE_EP				0x0

#define APPL_CFG_BASE_ADDR			0x104
#define APPL_CFG_BASE_ADDR_MASK			0xFFFFF000

#define APPL_CFG_IATU_DMA_BASE_ADDR		0x108
#define APPL_CFG_IATU_DMA_BASE_ADDR_MASK	0xFFFC0000

#define APPL_CFG_MISC				0x110
#define APPL_CFG_MISC_SLV_EP_MODE		BIT(14)
#define APPL_CFG_MISC_ARCACHE_MASK		0x3C00
#define APPL_CFG_MISC_ARCACHE_SHIFT		10
#define APPL_CFG_MISC_ARCACHE_VAL		3

#define APPL_CFG_SLCG_OVERRIDE			0x114
#define APPL_CFG_SLCG_OVERRIDE_SLCG_EN_MASTER	BIT(0)

#define APPL_CAR_RESET_OVRD				0x12C
#define APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N	BIT(0)

#define APPL_GTH_PHY			0x138
#define APPL_GTH_PHY_RST		0x1

#define EP_CFG_LINK_CAP				0x7C
#define EP_CFG_LINK_CAP_MAX_SPEED_MASK		0xF

#define EP_CS_STATUS_COMMAND			0x4
#define EP_CS_STATUS_COMMAND_BME		BIT(2)

#define PCIE_ATU_REGION_INDEX3	3 /* used for Prefetchable MEM accesses */

/* OUTBOUND */
#define TEGRA_PCIE_ATU_CR1			0x0
#define TEGRA_PCIE_ATU_TYPE_MEM			(0x0 << 0)
#define TEGRA_PCIE_ATU_TYPE_IO			(0x2 << 0)
#define TEGRA_PCIE_ATU_TYPE_CFG0		(0x4 << 0)
#define TEGRA_PCIE_ATU_TYPE_CFG1		(0x5 << 0)
#define TEGRA_PCIE_ATU_TYPE_TD_SHIFT		8
#define TEGRA_PCIE_ATU_INCREASE_REGION_SIZE	BIT(13)
#define TEGRA_PCIE_ATU_CR2			0x4
#define TEGRA_PCIE_ATU_ENABLE			(0x1 << 31)
#define TEGRA_PCIE_ATU_LOWER_BASE		0x8
#define TEGRA_PCIE_ATU_UPPER_BASE		0xC
#define TEGRA_PCIE_ATU_LIMIT			0x10
#define TEGRA_PCIE_ATU_LOWER_TARGET		0x14
#define TEGRA_PCIE_ATU_UPPER_TARGET		0x18
#define TEGRA_PCIE_ATU_UPPER_LIMIT		0x20

#define PCIE_ATU_BUS(x)			(((x) & 0xff) << 24)
#define PCIE_ATU_DEV(x)			(((x) & 0x1f) << 19)
#define PCIE_ATU_FUNC(x)		(((x) & 0x7) << 16)

#define IO_BASE_IO_DECODE				BIT(0)
#define IO_BASE_IO_DECODE_BIT8				BIT(8)

#define CFG_PREF_MEM_LIMIT_BASE				0x24
#define CFG_PREF_MEM_LIMIT_BASE_MEM_DECODE		BIT(0)
#define CFG_PREF_MEM_LIMIT_BASE_MEM_LIMIT_DECODE	BIT(16)

#define CFG_LINK_CAP			0x7C
#define CFG_LINK_CAP_MAX_LINK_SPEED_MASK	0xF
#define CFG_LINK_CAP_MAX_WIDTH_MASK		0x3F0
#define CFG_LINK_CAP_MAX_WIDTH_SHIFT		4

#define CFG_DEV_STATUS_CONTROL			0x78
#define CFG_DEV_STATUS_CONTROL_MPS_MASK		0xE0
#define CFG_DEV_STATUS_CONTROL_MPS_SHIFT	5

#define CFG_LINK_STATUS_CONTROL		0x80
#define CFG_LINK_STATUS_BW_MAN_STATUS	BIT(30)
#define CFG_LINK_STATUS_DLL_ACTIVE	BIT(29)
#define CFG_LINK_STATUS_LT		BIT(27)
#define CFG_LINK_CONTROL_LT		BIT(5)

#define CFG_LINK_STATUS_CONTROL_2	0xA0
#define CFG_LINK_STATUS_CONTROL_2_PCIE_CAP_EQ_CPL	BIT(17)
#define CFG_LINK_STATUS_CONTROL_2_TARGET_LS_MASK	0xF
#define CFG_LINK_STATUS_CONTROL_2_HW_AUTO_SPEED_DISABLE	BIT(5)

#define CFG_LINK_CAP_L1SUB		0x154

#define CAP_PL16G_STATUS_REG		0x164
#define CAP_PL16G_STATUS_REG_EQ_16G_CPL	BIT(0)

#define CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF	0x718
#define CFG_TIMER_CTRL_ACK_NAK_SHIFT	(19)

#define  PCI_L1SS_CAP_CM_RTM_MASK	0xFF00	/* Common mode restore time */
#define  PCI_L1SS_CAP_CM_RTM_SHIFT	8	/* Common mode restore mask */
#define  PCI_L1SS_CAP_PWRN_VAL_MASK	0xF80000 /* T_POWER_ON val mask */
#define  PCI_L1SS_CAP_PWRN_VAL_SHIFT	19	/* T_POWER_ON val shift */

#define EVENT_COUNTER_CONTROL_REG	0x168
#define EVENT_COUNTER_ALL_CLEAR		0x3
#define EVENT_COUNTER_ENABLE_ALL	0x7
#define EVENT_COUNTER_ENABLE_SHIFT	2
#define EVENT_COUNTER_EVENT_SEL_MASK	0xFF
#define EVENT_COUNTER_EVENT_SEL_SHIFT	16
#define EVENT_COUNTER_EVENT_Tx_L0S	0x2
#define EVENT_COUNTER_EVENT_Rx_L0S	0x3
#define EVENT_COUNTER_EVENT_L1		0x5
#define EVENT_COUNTER_EVENT_L1_1	0x7
#define EVENT_COUNTER_EVENT_L1_2	0x8
#define EVENT_COUNTER_GROUP_SEL_SHIFT	24
#define EVENT_COUNTER_GROUP_5		0x5

#define EVENT_COUNTER_DATA_REG		0x16C

#define DL_FEATURE_EXCHANGE_EN		BIT(31)

#define PORT_LOGIC_ACK_F_ASPM_CTRL			0x70C
#define ENTER_ASPM					BIT(30)
#define L0S_ENTRANCE_LAT_SHIFT				24
#define L0S_ENTRANCE_LAT_MASK				0x07000000
#define L1_ENTRANCE_LAT_SHIFT				27
#define L1_ENTRANCE_LAT_MASK				0x38000000
#define N_FTS_SHIFT					8
#define N_FTS_MASK					0xFF
#define N_FTS_VAL					52

#define PORT_LOGIC_GEN2_CTRL				0x80C
#define PORT_LOGIC_GEN2_CTRL_DIRECT_SPEED_CHANGE	BIT(17)
#define FTS_MASK					0xFF
#define FTS_VAL						52

#define PORT_LOGIC_MSI_CTRL_INT_0_EN		0x828

#define GEN3_EQ_CONTROL_OFF			0x8a8
#define GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT	8
#define GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK	GENMASK(23, 8)
#define GEN3_EQ_CONTROL_OFF_FB_MODE_MASK	GENMASK(3, 0)

#define GEN3_RELATED_OFF			0x890
#define GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL	BIT(0)
#define GEN3_RELATED_OFF_GEN3_EQ_DISABLE	BIT(16)
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_SHIFT	24
#define GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK	GENMASK(25, 24)

#define PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT	0x8D0
#define AMBA_ERROR_RESPONSE_CRS_SHIFT		3
#define AMBA_ERROR_RESPONSE_CRS_MASK		3
#define AMBA_ERROR_RESPONSE_CRS_OKAY		0
#define AMBA_ERROR_RESPONSE_CRS_OKAY_FFFFFFFF	1
#define AMBA_ERROR_RESPONSE_CRS_OKAY_FFFF0001	2

#define PORT_LOGIC_MSIX_DOORBELL			0x948

#define PORT_LOGIC_PL_CHK_REG_CONTROL_STATUS		0xB20
#define PORT_LOGIC_PL_CHK_REG_CHK_REG_START		BIT(0)
#define PORT_LOGIC_PL_CHK_REG_CHK_REG_CONTINUOUS	BIT(1)
#define PORT_LOGIC_PL_CHK_REG_CHK_REG_COMPARISON_ERROR	BIT(16)
#define PORT_LOGIC_PL_CHK_REG_CHK_REG_LOGIC_ERROR	BIT(17)
#define PORT_LOGIC_PL_CHK_REG_CHK_REG_COMPLETE		BIT(18)

#define PORT_LOGIC_MISC_CONTROL			0x8bc
#define PORT_LOGIC_MISC_CONTROL_DBI_RO_WR_EN	BIT(0)

#define PORT_LOGIC_PL_CHK_REG_ERR_ADDR		0xB28

#define CAP_SPCIE_CAP_OFF			0x154
#define CAP_SPCIE_CAP_OFF_DSP_TX_PRESET0_MASK	GENMASK(3, 0)
#define CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_MASK	GENMASK(11, 8)
#define CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_SHIFT	8

#define PL16G_CAP_OFF				0x188
#define PL16G_CAP_OFF_DSP_16G_TX_PRESET_MASK	GENMASK(3, 0)
#define PL16G_CAP_OFF_USP_16G_TX_PRESET_MASK	GENMASK(7, 4)
#define PL16G_CAP_OFF_USP_16G_TX_PRESET_SHIFT	4

#define MSIX_ADDR_MATCH_LOW_OFF		0x940
#define MSIX_ADDR_MATCH_LOW_OFF_EN	BIT(0)
#define MSIX_ADDR_MATCH_LOW_OFF_MASK	GENMASK(31, 2)

#define MSIX_ADDR_MATCH_HIGH_OFF	0x944
#define MSIX_ADDR_MATCH_HIGH_OFF_MASK	GENMASK(31, 0)

#define AUX_CLK_FREQ			0xB40

#define GEN4_LANE_MARGINING_1	0xb80
#define GEN4_LANE_MARGINING_1_NUM_TIMING_STEPS_MASK	GENMASK(5, 0)
#define GEN4_LANE_MARGINING_1_MAX_VOLTAGE_OFFSET_MASK	GENMASK(29, 24)
#define GEN4_LANE_MARGINING_1_MAX_VOLTAGE_OFFSET_SHIFT	24

#define GEN4_LANE_MARGINING_2	0xb84
#define GEN4_LANE_MARGINING_2_VOLTAGE_SUPPORTED		BIT(24)
#define GEN4_LANE_MARGINING_2_UP_DOWN_VOLTAGE		BIT(25)
#define GEN4_LANE_MARGINING_2_LEFT_RIGHT_TIMING		BIT(26)

#define DMA_RD_CHNL_NUM			2
#define DMA_WR_CHNL_NUM			4

#define LINK_RETRAIN_TIMEOUT HZ

/* DMA Common Registers */
#define DMA_WRITE_ENGINE_EN_OFF		0xC
#define DMA_WRITE_ENGINE_EN_OFF_ENABLE	BIT(0)

#define DMA_WRITE_DOORBELL_OFF		0x10
#define DMA_WRITE_DOORBELL_OFF_WR_STOP	BIT(31)

#define DMA_READ_ENGINE_EN_OFF		0x2C
#define DMA_READ_ENGINE_EN_OFF_ENABLE	BIT(0)

#define DMA_READ_DOORBELL_OFF		0x30
#define DMA_READ_DOORBELL_OFF_RD_STOP	BIT(31)

#define DMA_WRITE_INT_STATUS_OFF	0x4C
#define DMA_WRITE_INT_MASK_OFF		0x54
#define DMA_WRITE_INT_CLEAR_OFF		0x58

#define DMA_WRITE_DONE_IMWR_LOW_OFF	0x60
#define DMA_WRITE_DONE_IMWR_HIGH_OFF	0x64
#define DMA_WRITE_ABORT_IMWR_LOW_OFF	0x68
#define DMA_WRITE_ABORT_IMWR_HIGH_OFF	0x6C

#define DMA_WRITE_IMWR_DATA_OFF_BASE	0x70

#define DMA_READ_INT_STATUS_OFF		0xA0
#define DMA_READ_INT_MASK_OFF		0xA8
#define DMA_READ_INT_CLEAR_OFF		0xAC

#define DMA_READ_DONE_IMWR_LOW_OFF	0xCC
#define DMA_READ_DONE_IMWR_HIGH_OFF	0xD0
#define DMA_READ_ABORT_IMWR_LOW_OFF	0xD4
#define DMA_READ_ABORT_IMWR_HIGH_OFF	0xD8

#define DMA_READ_IMWR_DATA_OFF_BASE	0xDC

/* Channel specific registers */
#define DMA_CH_CONTROL1_OFF_WRCH	0x0
#define DMA_CH_CONTROL1_OFF_WRCH_LLE	BIT(9)
#define DMA_CH_CONTROL1_OFF_WRCH_RIE	BIT(4)
#define DMA_CH_CONTROL1_OFF_WRCH_LIE	BIT(3)
#define DMA_CH_CONTROL1_OFF_WRCH_LLP	BIT(2)
#define DMA_TRANSFER_SIZE_OFF_WRCH	0x8
#define DMA_SAR_LOW_OFF_WRCH		0xC
#define DMA_SAR_HIGH_OFF_WRCH		0x10
#define DMA_DAR_LOW_OFF_WRCH		0x14
#define DMA_DAR_HIGH_OFF_WRCH		0x18
#define DMA_LLP_LOW_OFF_WRCH		0x1C
#define DMA_LLP_HIGH_OFF_WRCH		0x20

#define DMA_CH_CONTROL1_OFF_RDCH	(0x0 + 0x100)
#define DMA_CH_CONTROL1_OFF_RDCH_LLE	BIT(9)
#define DMA_CH_CONTROL1_OFF_RDCH_RIE	BIT(4)
#define DMA_CH_CONTROL1_OFF_RDCH_LIE	BIT(3)
#define DMA_CH_CONTROL1_OFF_RDCH_LLP	BIT(2)
#define DMA_TRANSFER_SIZE_OFF_RDCH	(0x8 + 0x100)
#define DMA_SAR_LOW_OFF_RDCH		(0xC + 0x100)
#define DMA_SAR_HIGH_OFF_RDCH		(0x10 + 0x100)
#define DMA_DAR_LOW_OFF_RDCH		(0x14 + 0x100)
#define DMA_DAR_HIGH_OFF_RDCH		(0x18 + 0x100)
#define DMA_LLP_LOW_OFF_RDCH		(0x1C + 0x100)
#define DMA_LLP_HIGH_OFF_RDCH		(0x20 + 0x100)

#define TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_SHIFT (15)
#define TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_HUB2 (2)

#define PME_ACK_TIMEOUT 10000

#define LTSSM_TIMEOUT 25000	/* 25ms */

#define NUM_TIMING_STEPS 0x14
#define NUM_VOLTAGE_STEPS 0x14

#define DMA_TEST_BUF_SIZE SZ_512M

#define LTR_MSG_TIMEOUT (100 * 1000)

#define PERST_DEBOUNCE_TIME (5 * 1000)

#define EVENT_QUEUE_LEN (256)

/* Max error count limit is 0x3f, payload=(0xc0 | 0x3f) */
#define MAX_ERR_CNT_PAYLOAD 0xff
#define NORMAL_PAYLOAD 0x0f
#define CLR_ERR_PAYLOAD 0x55
/* payload[6] = 1 Left step margin
 * payload[6] = 0 Right step margin
 * payload[7] = 1 down step margin
 * payload[7] = 0 up step margin
 */
#define LEFT_STEP_PAYLOAD (0x1 << 6)
#define RIGHT_STEP_PAYLOAD (0x0 << 6)
#define DOWN_STEP_PAYLOAD (0x1 << 7)
#define UP_STEP_PAYLOAD (0x0 << 7)

#define LEFT_STEP 'L'
#define RIGHT_STEP 'R'
#define NO_STEP 'N'
#define DOWN_STEP 'D'
#define UP_STEP 'U'

/* Receiver number*/
#define RP_RCV_NO 1
#define EP_RCV_NO 6

/* Time in msec */
#define MARGIN_WIN_TIME 1000
#define MARGIN_READ_DELAY 100

enum ep_event {
	EP_EVENT_NONE = 0,
	EP_PEX_RST_DEASSERT,
	EP_PEX_RST_ASSERT,
	EP_PEX_HOT_RST_DONE,
	EP_PEX_BME_CHANGE,
	EP_EVENT_EXIT,
	EP_EVENT_INVALID,
};

enum margin_cmds {
	MARGIN_SET_ERR_COUNT,
	MARGIN_SET_NO_CMD,
	MARGIN_SET_X_OFFSET,
	MARGIN_SET_Y_OFFSET,
	MARGIN_SET_NORMAL,
	MARGIN_CLR_ERR,
};

struct margin_cmd {
	int margin_type;
	int rcv_no;
	int payload;
	int rxm_payload_check;
	int rxm_cmd_check;
};

struct tegra_pcie_dw {
	struct device *dev;
	struct resource	*dbi_res;
	struct resource	*atu_dma_res;
	void __iomem		*appl_base;
	struct clk		*core_clk;
	struct clk		*core_clk_m;
	struct reset_control	*core_apb_rst;
	struct reset_control	*core_rst;
	struct dw_pcie pci;
	enum dw_pcie_device_mode mode;

	/* EP mode specific */
	u16 device_id;
	struct task_struct *pcie_ep_task;
	wait_queue_head_t wq;
	int pex_rst_gpio;
	int pex_rst_irq;
	int ep_state;
	DECLARE_KFIFO(event_fifo, u32, EVENT_QUEUE_LEN);

	int			phy_count;	/* DT phy-names count */
	struct phy		**phy;

	struct dentry *debugfs;
	u32 target_speed;
	void *cpu_virt_addr;
	bool disable_clock_request;
	bool enable_srns;
	bool ep_mode_slot_supplies_en;
	bool power_down_en;
	bool is_safety_platform;
	bool td_bit;
	bool disable_l1_cpm;
	u8 init_link_width;

	struct tegra_bwmgr_client *emc_bw;

#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
	/* DMA operation */
	dma_addr_t dma_addr;
	u64 src;
	u64 dst;
	u32 size;
	u8 channel;
	bool dma_poll;
	/* lock for write DMA channel */
	struct mutex wr_lock[DMA_WR_CHNL_NUM];
	/* lock for read DMA channel */
	struct mutex rd_lock[DMA_RD_CHNL_NUM];
	struct completion wr_cpl[DMA_WR_CHNL_NUM];
	struct completion rd_cpl[DMA_RD_CHNL_NUM];
	ktime_t wr_start_time;
	ktime_t wr_end_time;
	ktime_t rd_start_time;
	ktime_t rd_end_time;
	unsigned long wr_busy;
	unsigned long rd_busy;
#endif

	u32 cfg_link_cap_l1sub;
	u32 cap_pl16g_status;
	u32 cap_pl16g_cap_off;
	u32 event_cntr_ctrl;
	u32 event_cntr_data;
	u32 dl_feature_cap;

	u32 num_lanes;
	u32 max_speed;
	u32 disabled_aspm_states;
	u32 init_speed;
	bool cdm_check;
	u32 cid;
	u32 msi_ctrl_int;
	int pex_wake;
	u32 tsa_config_addr;
	bool link_state;
	u32 aux_clk_freq;
	u32 preset_init;
	u32 aspm_cmrt;
	u32 aspm_pwr_on_t;
	u32 aspm_l0s_enter_lat;
	bool update_fc_fixup;

	int n_gpios;
	int *gpios;

	struct regulator *pex_ctl_reg;
	struct regulator *slot_ctl_3v3;
	struct regulator *slot_ctl_12v;
	struct margin_cmd mcmd;
	u32 dvfs_tbl[4][4]; /* for x1/x2/x3/x4 and Gen-1/2/3/4 */
};

struct tegra_pcie_of_data {
	enum dw_pcie_device_mode mode;
};

struct dma_tx {
	u64 src;
	u64 dst;
	u32 size;
	u8 channel;
	bool ll;
};

struct dma_ll_element_1 {
	u32 cb:1;
	u32 tcb:1;
	u32 llp:1;
	u32 lie:1;
	u32 rie:1;
};

struct dma_ll {
	struct dma_ll_element_1 ele_1;
	u32 size;
	u32 sar_low;
	u32 sar_high;
	u32 dar_low;
	u32 dar_high;
};

#define EP_STATE_DISABLED	0
#define EP_STATE_ENABLED	1

static unsigned int pcie_emc_client_id[] = {
	TEGRA_BWMGR_CLIENT_PCIE,
	TEGRA_BWMGR_CLIENT_PCIE_1,
	TEGRA_BWMGR_CLIENT_PCIE_2,
	TEGRA_BWMGR_CLIENT_PCIE_3,
	TEGRA_BWMGR_CLIENT_PCIE_4,
	TEGRA_BWMGR_CLIENT_PCIE_5
};

#define GEN1_CORE_CLK_FREQ	62500000
#define GEN2_CORE_CLK_FREQ	125000000
#define GEN3_CORE_CLK_FREQ	250000000
#define GEN4_CORE_CLK_FREQ	500000000

static unsigned int pcie_gen_freq[] = {
	GEN1_CORE_CLK_FREQ,
	GEN2_CORE_CLK_FREQ,
	GEN3_CORE_CLK_FREQ,
	GEN4_CORE_CLK_FREQ
};

static void tegra_pcie_downstream_dev_to_D0(struct tegra_pcie_dw *pcie);
static int tegra_pcie_dw_pme_turnoff(struct tegra_pcie_dw *pcie);
static int tegra_pcie_dw_runtime_suspend(struct device *dev);
static int tegra_pcie_dw_runtime_resume(struct device *dev);
static int tegra_pcie_dw_link_up(struct dw_pcie *pci);

static void tegra_pcie_set_dbi_writable(struct dw_pcie *pci, bool on)
{
	u32 val;

	val = readl(pci->dbi_base + PORT_LOGIC_MISC_CONTROL);
	if (on)
		val = val | PORT_LOGIC_MISC_CONTROL_DBI_RO_WR_EN;
	else
		val = val & ~PORT_LOGIC_MISC_CONTROL_DBI_RO_WR_EN;
	writel(val, pci->dbi_base + PORT_LOGIC_MISC_CONTROL);
}

static void tegra_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base,
				 u32 reg, size_t size, u32 val)
{
	int ret;

	tegra_pcie_set_dbi_writable(pci, true);
	ret = dw_pcie_write(base + reg, size, val);
	if (ret)
		dev_err(pci->dev, "write DBI address failed\n");
	tegra_pcie_set_dbi_writable(pci, false);
}

static inline void dma_common_wr16(void __iomem *p, u32 val, u32 offset)
{
	writew(val, 0x20000 + offset + p);
}

static inline u16 dma_common_rd16(void __iomem *p, u32 offset)
{
	return readw(0x20000 + offset + p);
}

static inline void dma_common_wr(void __iomem *p, u32 val, u32 offset)
{
	writel(val, 0x20000 + offset + p);
}

static inline u32 dma_common_rd(void __iomem *p, u32 offset)
{
	return readl(0x20000 + offset + p);
}

static inline void dma_channel_wr(void __iomem *p, u8 channel, u32 val,
				  u32 offset)
{
	writel(val, 0x20000 + (0x200 * (channel + 1)) + offset + p);
}

static inline u32 dma_channel_rd(void __iomem *p, u8 channel, u32 offset)
{
	return readl(0x20000 + (0x200 * (channel + 1)) + offset + p);
}

static void check_apply_link_bad_war(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	u32 val;

	dw_pcie_read(pci->dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	if ((val >> 16) & PCI_EXP_LNKSTA_LBMS) {
		if (pcie->init_link_width >
		    ((val >> 16) & PCI_EXP_LNKSTA_NLW) >>
		    PCI_EXP_LNKSTA_NLW_SHIFT) {
			dev_warn(pci->dev, "PCIe link is bad, width reduced\n");
			dw_pcie_read(pci->dbi_base +
					 CFG_LINK_STATUS_CONTROL_2, 4, &val);
			val &= ~PCI_EXP_LNKSTA_CLS;
			val |= PCI_EXP_LNKSTA_CLS_2_5GB;
			dw_pcie_write(pci->dbi_base +
					  CFG_LINK_STATUS_CONTROL_2, 4, val);

			dw_pcie_read(pci->dbi_base +
					 CFG_LINK_STATUS_CONTROL, 4, &val);
			val |= CFG_LINK_CONTROL_LT;
			dw_pcie_write(pci->dbi_base +
					  CFG_LINK_STATUS_CONTROL, 4, val);
			/* NOTE:- Since this scenario is uncommon and link as
			 * such is not stable anyway, not waiting to confirm
			 * if link is really transiting to Gen-2 speed
			 */
		}
	}
}

static irqreturn_t tegra_pcie_rp_irq_handler(struct tegra_pcie_dw *pcie)
{
	u32 val, tmp;
	int handled;
	struct dw_pcie *pci = &pcie->pci;
	struct pcie_port *pp = &pci->pp;

	handled = 1;

	val = readl(pcie->appl_base + APPL_INTR_STATUS_L0);
	dev_dbg(pci->dev, "APPL_INTR_STATUS_L0 = 0x%08X\n", val);
	if (val & APPL_INTR_STATUS_L0_LINK_STATE_INT) {
		val = readl(pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
		dev_dbg(pci->dev, "APPL_INTR_STATUS_L1_0_0 = 0x%08X\n", val);
		if (val & APPL_INTR_STATUS_L1_0_0_LINK_REQ_RST_NOT_CHGED) {
			writel(val, pcie->appl_base + APPL_INTR_STATUS_L1_0_0);

			/* SBR & Surprise Link Down WAR */
			val = readl(pcie->appl_base + APPL_CAR_RESET_OVRD);
			val &= ~APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N;
			writel(val, pcie->appl_base + APPL_CAR_RESET_OVRD);
			udelay(1);
			val = readl(pcie->appl_base + APPL_CAR_RESET_OVRD);
			val |= APPL_CAR_RESET_OVRD_CYA_OVERRIDE_CORE_RST_N;
			writel(val, pcie->appl_base + APPL_CAR_RESET_OVRD);

			dw_pcie_read(pci->dbi_base + PORT_LOGIC_GEN2_CTRL, 4,
				     &val);
			val |= PORT_LOGIC_GEN2_CTRL_DIRECT_SPEED_CHANGE;
			dw_pcie_write(pci->dbi_base + PORT_LOGIC_GEN2_CTRL, 4,
				      val);
		}
	}
	if (val & APPL_INTR_STATUS_L0_INT_INT) {
		val = readl(pcie->appl_base + APPL_INTR_STATUS_L1_8_0);
		dev_dbg(pci->dev, "APPL_INTR_STATUS_L1_8_0 = 0x%08X\n", val);
#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
		if (val & APPL_INTR_STATUS_L1_8_0_EDMA_INT_MASK) {
			val = dma_common_rd(pci->atu_base,
					    DMA_WRITE_INT_STATUS_OFF);
			/* check the status of all busy marked channels */
			for_each_set_bit(tmp, &pcie->wr_busy,
					 DMA_WR_CHNL_NUM) {
				if (BIT(tmp) & val) {
					dma_common_wr(pci->atu_base,
						      BIT(tmp),
						      DMA_WRITE_INT_CLEAR_OFF);
					/* send completion to channel */
					complete(&pcie->wr_cpl[tmp]);
					/* clear status */
					pcie->wr_busy &= ~(BIT(tmp));
				}
			}

			val = dma_common_rd(pci->atu_base,
					    DMA_READ_INT_STATUS_OFF);
			/* check the status of all busy marked channels */
			for_each_set_bit(tmp, &pcie->rd_busy,
					 DMA_RD_CHNL_NUM) {
				if (BIT(tmp) & val) {
					dma_common_wr(pci->atu_base,
						      BIT(tmp),
						      DMA_READ_INT_CLEAR_OFF);
					/* send completion to channel */
					complete(&pcie->rd_cpl[tmp]);
					/* clear status */
					pcie->rd_busy &= ~(BIT(tmp));
				}
			}
		}
#endif
		if (val & APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS) {
			writel(APPL_INTR_STATUS_L1_8_0_AUTO_BW_INT_STS,
			       pcie->appl_base + APPL_INTR_STATUS_L1_8_0);
			check_apply_link_bad_war(pp);
		}
		if (val & APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS) {
			writel(APPL_INTR_STATUS_L1_8_0_BW_MGT_INT_STS,
			       pcie->appl_base + APPL_INTR_STATUS_L1_8_0);

			dw_pcie_read(pci->dbi_base +
					 CFG_LINK_STATUS_CONTROL, 4, &val);
			dev_dbg(pci->dev, "Link Speed : Gen-%u\n", (val >> 16) &
					   PCI_EXP_LNKSTA_CLS);
		}
	}
	val = readl(pcie->appl_base + APPL_INTR_STATUS_L0);
	if (val & APPL_INTR_STATUS_L0_CDM_REG_CHK_INT) {
		val = readl(pcie->appl_base + APPL_INTR_STATUS_L1_18);
		dw_pcie_read(pci->dbi_base +
				 PORT_LOGIC_PL_CHK_REG_CONTROL_STATUS, 4, &tmp);
		dev_dbg(pci->dev, "APPL_INTR_STATUS_L1_18 = 0x%08X\n", val);
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMPLT) {
			dev_err(pci->dev, "CDM check complete\n");
			tmp |= PORT_LOGIC_PL_CHK_REG_CHK_REG_COMPLETE;
		}
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_CMP_ERR) {
			dev_err(pci->dev, "CDM comparison mismatch\n");
			tmp |= PORT_LOGIC_PL_CHK_REG_CHK_REG_COMPARISON_ERROR;
		}
		if (val & APPL_INTR_STATUS_L1_18_CDM_REG_CHK_LOGIC_ERR) {
			dev_err(pci->dev, "CDM Logic error\n");
			tmp |= PORT_LOGIC_PL_CHK_REG_CHK_REG_LOGIC_ERROR;
		}
		dw_pcie_write(pci->dbi_base +
				 PORT_LOGIC_PL_CHK_REG_CONTROL_STATUS, 4, tmp);
		dw_pcie_read(pci->dbi_base +
				 PORT_LOGIC_PL_CHK_REG_ERR_ADDR, 4, &tmp);
		dev_err(pci->dev, "CDM Error Address Offset = 0x%08X\n", tmp);
	}

	return IRQ_RETVAL(handled);
}

static irqreturn_t tegra_pcie_ep_irq_handler(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie_ep *ep = &pcie->pci.ep;
	u32 val = 0, tmp;

	val = readl(pcie->appl_base + APPL_INTR_STATUS_L0);
	dev_dbg(pcie->dev, "APPL_INTR_STATUS_L0 = 0x%08X\n", val);
	if (val & APPL_INTR_STATUS_L0_PEX_RST_INT) {
		/* clear any stale PEX_RST interrupt */
		writel(APPL_INTR_STATUS_L0_PEX_RST_INT,
		       pcie->appl_base + APPL_INTR_STATUS_L0);
		if (!kfifo_put(&pcie->event_fifo, EP_PEX_RST_DEASSERT)) {
			dev_err(pcie->dev, "EVENT: fifo is full\n");
			return IRQ_HANDLED;
		}
		wake_up(&pcie->wq);
	} else if (val & APPL_INTR_STATUS_L0_LINK_STATE_INT) {
		val = readl(pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
		writel(val, pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
		dev_dbg(pcie->dev, "APPL_INTR_STATUS_L1_0_0 = 0x%08X\n", val);
		if (val & APPL_INTR_STATUS_L1_0_0_HOT_RESET_DONE) {
			/* clear any stale PEX_RST interrupt */
			if (!kfifo_put(&pcie->event_fifo,
				       EP_PEX_HOT_RST_DONE)) {
				dev_err(pcie->dev, "EVENT: fifo is full\n");
				return IRQ_HANDLED;
			}
			wake_up(&pcie->wq);
		}
		if (val & APPL_INTR_STATUS_L1_0_0_RDLH_LINK_UP_CHGED) {
			tmp = readl(pcie->appl_base + APPL_LINK_STATUS);
			if (tmp & APPL_LINK_STATUS_RDLH_LINK_UP) {
				dev_dbg(pcie->dev, "link is up\n");
				dw_pcie_ep_linkup(ep);
			}
		}
	} else if (val & APPL_INTR_STATUS_L0_PCI_CMD_EN_INT) {
		val = readl(pcie->appl_base + APPL_INTR_STATUS_L1_15);
		writel(val, pcie->appl_base + APPL_INTR_STATUS_L1_15);
		dev_dbg(pcie->dev, "APPL_INTR_STATUS_L1_15 = 0x%08X\n", val);
		if (val & APPL_INTR_STATUS_L1_15_CFG_BME_CHGED) {
			if (!kfifo_put(&pcie->event_fifo, EP_PEX_BME_CHANGE)) {
				dev_err(pcie->dev, "EVENT: fifo is full\n");
				return IRQ_HANDLED;
			}
			wake_up(&pcie->wq);
		}
	} else {
		dev_info(pcie->dev, "Random interrupt (STATUS = 0x%08X)\n",
			 val);
		writel(val, pcie->appl_base + APPL_INTR_STATUS_L0);
	}

	return IRQ_HANDLED;
}

static irqreturn_t tegra_pcie_irq_handler(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)arg;

	if (pcie->mode == DW_PCIE_RC_TYPE)
		return tegra_pcie_rp_irq_handler(pcie);
	else if (pcie->mode == DW_PCIE_EP_TYPE)
		return tegra_pcie_ep_irq_handler(pcie);

	return IRQ_NONE;
}

static int bpmp_send_uphy_message_atomic(struct mrq_uphy_request *req, int size,
					 struct mrq_uphy_response *reply,
					 int reply_size)
{
	unsigned long flags;
	int err;

	local_irq_save(flags);
	err = tegra_bpmp_send_receive_atomic(MRQ_UPHY, req, size, reply,
					     reply_size);
	local_irq_restore(flags);

	return err;
}

static int bpmp_send_uphy_message(struct mrq_uphy_request *req, int size,
				  struct mrq_uphy_response *reply,
				  int reply_size)
{
	int err;

	err = tegra_bpmp_send_receive(MRQ_UPHY, req, size, reply, reply_size);
	if (err != -EAGAIN)
		return err;

	/*
	 * in case the mail systems worker threads haven't been started yet,
	 * use the atomic send/receive interface. This happens because the
	 * clocks are initialized before the IPC mechanism.
	 */
	return bpmp_send_uphy_message_atomic(req, size, reply, reply_size);
}

static int uphy_bpmp_pcie_controller_state_set(int controller, int enable)
{
	struct mrq_uphy_request req;
	struct mrq_uphy_response resp;

	req.cmd = CMD_UPHY_PCIE_CONTROLLER_STATE;
	req.controller_state.pcie_controller = controller;
	req.controller_state.enable = enable;

	return bpmp_send_uphy_message(&req, sizeof(req), &resp, sizeof(resp));
}

static irqreturn_t tegra_pcie_msi_irq_handler(int irq, void *arg)
{
	struct pcie_port *pp = arg;

	return dw_handle_msi_irq(pp);
}

static inline void prog_atu(struct pcie_port *pp, int i, u32 val, u32 reg)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	writel(val, pci->atu_base + (i * 0x200) + reg);
}

static void outbound_atu(struct pcie_port *pp, int i, int type, u64 cpu_addr,
			 u64 pci_addr, u64 size)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);

	prog_atu(pp, i, lower_32_bits(cpu_addr), TEGRA_PCIE_ATU_LOWER_BASE);
	prog_atu(pp, i, upper_32_bits(cpu_addr), TEGRA_PCIE_ATU_UPPER_BASE);
	prog_atu(pp, i, lower_32_bits(cpu_addr + size - 1),
		 TEGRA_PCIE_ATU_LIMIT);
	prog_atu(pp, i, upper_32_bits(cpu_addr + size - 1),
		 TEGRA_PCIE_ATU_UPPER_LIMIT);
	prog_atu(pp, i, lower_32_bits(pci_addr), TEGRA_PCIE_ATU_LOWER_TARGET);
	prog_atu(pp, i, upper_32_bits(pci_addr), TEGRA_PCIE_ATU_UPPER_TARGET);
	prog_atu(pp, i, type | TEGRA_PCIE_ATU_INCREASE_REGION_SIZE |
		 pcie->td_bit << TEGRA_PCIE_ATU_TYPE_TD_SHIFT,
		 TEGRA_PCIE_ATU_CR1);
	prog_atu(pp, i, PCIE_ATU_ENABLE, TEGRA_PCIE_ATU_CR2);
}

static int tegra_pcie_dw_rd_own_conf(struct pcie_port *pp, int where, int size,
				     u32 *val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/* This is EP specific register and system hangs when it is
	 * accessed with link being in ASPM-L1 state.
	 * So skip accessing it altogether
	 */
	if (where == PORT_LOGIC_MSIX_DOORBELL) {
		*val = 0x00000000;
		return PCIBIOS_SUCCESSFUL;
	} else {
		return dw_pcie_read(pci->dbi_base + where, size, val);
	}
}

static int tegra_pcie_dw_rd_other_conf(struct pcie_port *pp,
				       struct pci_bus *bus, unsigned int devfn,
				       int where, int size, u32 *val)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == pp->root_bus_nr)
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	cpu_addr = pp->cfg1_base;
	cfg_size = pp->cfg1_size;
	va_cfg_base = pp->va_cfg1_base;

	outbound_atu(pp, PCIE_ATU_REGION_INDEX0, type, cpu_addr, busdev,
		     cfg_size);
	ret = dw_pcie_read(va_cfg_base + where, size, val);
	return ret;
}

#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
static int dma_write(struct tegra_pcie_dw *pcie, struct dma_tx *tx)
{
	struct dw_pcie *pci = &pcie->pci;
	struct device *dev = pcie->dev;
	u32 val = 0, bit = 0;
	int ret = 0;
	unsigned long now, timeout = msecs_to_jiffies(6000);

	if (tx->channel > 3) {
		dev_err(dev, "Invalid channel num, should be within [0~3]\n");
		return -EINVAL;
	}

	/* acquire lock for channel HW */
	mutex_lock(&pcie->wr_lock[tx->channel]);

	/* program registers */
	/* Enable Write Engine */
	dma_common_wr(pci->atu_base, DMA_WRITE_ENGINE_EN_OFF_ENABLE,
		      DMA_WRITE_ENGINE_EN_OFF);

	/* Un Mask DONE and ABORT interrupts */
	val = dma_common_rd(pci->atu_base, DMA_WRITE_INT_MASK_OFF);
	val &= ~(1 << tx->channel);		/* DONE */
	val &= ~(1 << ((tx->channel) + 16));	/* ABORT */
	dma_common_wr(pci->atu_base, val, DMA_WRITE_INT_MASK_OFF);

	val = dma_channel_rd(pci->atu_base, tx->channel,
			     DMA_CH_CONTROL1_OFF_WRCH);
	if (tx->ll)
		val = DMA_CH_CONTROL1_OFF_WRCH_LLE;
	else
		val = DMA_CH_CONTROL1_OFF_WRCH_LIE;
	dma_channel_wr(pci->atu_base, tx->channel, val,
		       DMA_CH_CONTROL1_OFF_WRCH);

	if (tx->ll) {
		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->src & 0xFFFFFFFF),
			       DMA_LLP_LOW_OFF_WRCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->src >> 32) & 0xFFFFFFFF),
			       DMA_LLP_HIGH_OFF_WRCH);
	} else {
		dma_channel_wr(pci->atu_base, tx->channel, tx->size,
			       DMA_TRANSFER_SIZE_OFF_WRCH);

		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->src & 0xFFFFFFFF),
			       DMA_SAR_LOW_OFF_WRCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->src >> 32) & 0xFFFFFFFF),
			       DMA_SAR_HIGH_OFF_WRCH);

		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->dst & 0xFFFFFFFF),
			       DMA_DAR_LOW_OFF_WRCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->dst >> 32) & 0xFFFFFFFF),
			       DMA_DAR_HIGH_OFF_WRCH);
	}
	/* acquire lock for busy-data and mark it as busy and then release */
	pcie->wr_busy |= 1 << tx->channel;

	pcie->wr_start_time = ktime_get();
	/* start DMA (ring the door bell) */
	/* ring the door bell with channel number */
	dma_common_wr(pci->atu_base, pcie->channel,
		      DMA_WRITE_DOORBELL_OFF);

	if (pcie->dma_poll) {
		now = jiffies;
		while (true) {
			val = dma_common_rd(pci->atu_base,
					    DMA_WRITE_INT_STATUS_OFF);
			/* check the status of all busy marked channels */
			for_each_set_bit(bit, &pcie->wr_busy, DMA_WR_CHNL_NUM) {
				if (BIT(bit) & val) {
					pcie->wr_end_time = ktime_get();
					dma_common_wr(pci->atu_base,
						      BIT(bit),
						      DMA_WRITE_INT_CLEAR_OFF);
					/* clear status */
					pcie->wr_busy &= ~(BIT(bit));
				}
			}
			if (!pcie->wr_busy)
				break;
			if (time_after(jiffies, now + timeout)) {
				dev_err(dev, "DMA write timed out & poll end\n");
				ret = -ETIMEDOUT;
				/* if timeout, clear the mess, sanitize channel
				 * & return err
				 */
				dma_common_wr(pci->atu_base,
					      DMA_WRITE_DOORBELL_OFF_WR_STOP |
					      tx->channel,
					      DMA_WRITE_DOORBELL_OFF);
				goto exit;
			}
		}
		dev_info(dev, "DMA write. Size: %u bytes, Time diff: %lld ns\n",
			 tx->size, ktime_to_ns(pcie->wr_end_time) -
			 ktime_to_ns(pcie->wr_start_time));
	} else {
		/* wait for completion or timeout */
		ret = wait_for_completion_timeout(&pcie->wr_cpl[tx->channel],
						  msecs_to_jiffies(5000));
		if (ret == 0) {
			dev_err(dev, "DMA write timed out and no interrupt\n");
			ret = -ETIMEDOUT;
			/* if timeout, clear the mess, sanitize channel &
			 * return err
			 */
			dma_common_wr(pci->atu_base,
				      DMA_WRITE_DOORBELL_OFF_WR_STOP |
				      tx->channel,
				      DMA_WRITE_DOORBELL_OFF);
			goto exit;
		}
	}

exit:
	mutex_unlock(&pcie->wr_lock[tx->channel]);
	return ret;
}

static int dma_read(struct tegra_pcie_dw *pcie, struct dma_tx *tx)
{
	struct dw_pcie *pci = &pcie->pci;
	struct device *dev = pcie->dev;
	u32 val = 0, bit = 0;
	int ret = 0;
	unsigned long now, timeout = msecs_to_jiffies(6000);

	if (tx->channel > 1) {
		dev_err(dev, "Invalid channel num, should be within [0~1]\n");
		return -EINVAL;
	}

	/* acquire lock for channel HW */
	mutex_lock(&pcie->rd_lock[tx->channel]);

	/* program registers */
	/* Enable Read Engine */
	dma_common_wr(pci->atu_base, DMA_READ_ENGINE_EN_OFF_ENABLE,
		      DMA_READ_ENGINE_EN_OFF);

	/* Un Mask DONE and ABORT interrupts */
	val = dma_common_rd(pci->atu_base, DMA_READ_INT_MASK_OFF);
	val &= ~(1 << tx->channel);		/* DONE */
	val &= ~(1 << ((tx->channel) + 16));	/* ABORT */
	dma_common_wr(pci->atu_base, val, DMA_READ_INT_MASK_OFF);

	val = dma_channel_rd(pci->atu_base, tx->channel,
			     DMA_CH_CONTROL1_OFF_RDCH);
	if (tx->ll)
		val = DMA_CH_CONTROL1_OFF_RDCH_LLE;
	else
		val = DMA_CH_CONTROL1_OFF_RDCH_LIE;
	dma_channel_wr(pci->atu_base, tx->channel, val,
		       DMA_CH_CONTROL1_OFF_RDCH);

	if (tx->ll) {
		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->src & 0xFFFFFFFF),
			       DMA_LLP_LOW_OFF_RDCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->src >> 32) & 0xFFFFFFFF),
			       DMA_LLP_HIGH_OFF_RDCH);
	} else {
		dma_channel_wr(pci->atu_base, tx->channel, tx->size,
			       DMA_TRANSFER_SIZE_OFF_RDCH);

		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->src & 0xFFFFFFFF),
			       DMA_SAR_LOW_OFF_RDCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->src >> 32) & 0xFFFFFFFF),
			       DMA_SAR_HIGH_OFF_RDCH);

		dma_channel_wr(pci->atu_base, tx->channel,
			       (tx->dst & 0xFFFFFFFF),
			       DMA_DAR_LOW_OFF_RDCH);
		dma_channel_wr(pci->atu_base, tx->channel,
			       ((tx->dst >> 32) & 0xFFFFFFFF),
			       DMA_DAR_HIGH_OFF_RDCH);
	}

	/* acquire lock for busy-data and mark it as busy and then release */
	pcie->rd_busy |= 1 << tx->channel;

	pcie->rd_start_time = ktime_get();
	/* start DMA (ring the door bell) */
	/* ring the door bell with channel number */
	dma_common_wr(pci->atu_base, pcie->channel,
		      DMA_READ_DOORBELL_OFF);

	if (pcie->dma_poll) {
		now = jiffies;
		while (true) {
			val = dma_common_rd(pci->atu_base,
					    DMA_READ_INT_STATUS_OFF);
			/* check the status of all busy marked channels */
			for_each_set_bit(bit, &pcie->rd_busy, DMA_RD_CHNL_NUM) {
				if (BIT(bit) & val) {
					pcie->rd_end_time = ktime_get();
					dma_common_wr(pci->atu_base,
						      BIT(bit),
						      DMA_READ_INT_CLEAR_OFF);
					/* clear status */
					pcie->rd_busy &= ~(BIT(bit));
				}
			}
			if (!pcie->rd_busy)
				break;
			if (time_after(jiffies, now + timeout)) {
				dev_err(dev, "DMA read timed out & poll end\n");
				ret = -ETIMEDOUT;
				/* if timeout, clear the mess, sanitize channel
				 * & return err
				 */
				dma_common_wr(pci->atu_base,
					      DMA_READ_DOORBELL_OFF_RD_STOP |
					      tx->channel,
					      DMA_READ_DOORBELL_OFF);
				goto exit;
			}
		}
		dev_info(dev, "DMA read. Size: %u bytes, Time diff: %lld ns\n",
			 tx->size, ktime_to_ns(pcie->rd_end_time) -
			 ktime_to_ns(pcie->rd_start_time));
	} else {
		/* wait for completion or timeout */
		ret = wait_for_completion_timeout(&pcie->rd_cpl[tx->channel],
						  msecs_to_jiffies(5000));
		if (ret == 0) {
			dev_err(dev, "DMA read timed out and no interrupt\n");
			ret = -ETIMEDOUT;
			/* if timeout, clear the mess, sanitize channel
			 * & return err
			 */
			dma_common_wr(pci->atu_base,
				      DMA_READ_DOORBELL_OFF_RD_STOP |
				      tx->channel,
				      DMA_READ_DOORBELL_OFF);
			goto exit;
		}
	}

exit:
	mutex_unlock(&pcie->rd_lock[tx->channel]);
	return ret;
}

static int write(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct dma_tx tx;
	int ret = 0;
	void __iomem *dst_cpu_virt;

	memset(&tx, 0x0, sizeof(struct dma_tx));
	tx.src = pcie->src;
	tx.dst = pcie->dst;
	tx.size = pcie->size;
	tx.channel = pcie->channel;

	dst_cpu_virt = ioremap_nocache(pcie->dst, pcie->size);

	/* fill source with random data */
	get_random_bytes(pcie->cpu_virt_addr, pcie->size);

	ret = dma_write(pcie, &tx);
	if (ret < 0) {
		dev_err(pcie->dev, "DMA-Write test FAILED\n");
		ret = -EIO;
		goto err_out;
	}

	/* compare copied data */
	if (!memcmp(pcie->cpu_virt_addr, dst_cpu_virt, pcie->size))
		dev_info(pcie->dev, "DMA-Write test PASSED\n");
	else
		dev_info(pcie->dev, "DMA-Write test FAILED\n");

err_out:
	iounmap(dst_cpu_virt);
	return ret;
}

static int write_ll(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct dma_tx tx;
	int ret = 0;
	struct dma_ll *ll;
	void __iomem *dst_cpu_virt;

	dst_cpu_virt = ioremap_nocache(pcie->dst, 6 * 64 * 1024);

	/* create linked list */
	ll = (struct dma_ll *)(pcie->cpu_virt_addr);

	/* leave first 64K for LL element preparation */
	memset((ll + 0), 0x0, sizeof(struct dma_ll));
	(ll + 0)->size = (64 * 1024);
	(ll + 0)->sar_low = pcie->src + (64 * 1024);
	(ll + 0)->dar_low = (pcie->dst + (64 * 1024)) & 0xFFFFFFFF;
	(ll + 0)->dar_high = ((pcie->dst + (64 * 1024)) >> 32) & 0xFFFFFFFF;
	get_random_bytes((u8 *)pcie->cpu_virt_addr + (64 * 1024), 64 * 1024);

	memset((ll + 1), 0x0, sizeof(struct dma_ll));
	(ll + 1)->size = (64 * 1024);
	(ll + 1)->sar_low = pcie->src + (64 * 1024 * 2);
	(ll + 1)->dar_low = (pcie->dst + (64 * 1024 * 2)) & 0xFFFFFFFF;
	(ll + 1)->dar_high = ((pcie->dst + (64 * 1024 * 2)) >> 32) & 0xFFFFFFFF;
	get_random_bytes((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 2),
			 64 * 1024);

	memset((ll + 2), 0x0, sizeof(struct dma_ll));
	(ll + 2)->ele_1.llp = 1;
	(ll + 2)->sar_low = (4 * sizeof(struct dma_ll)) + pcie->src;

	memset((ll + 4), 0x0, sizeof(struct dma_ll));
	(ll + 4)->ele_1.lie = 1;
	(ll + 4)->size = (64 * 1024);
	(ll + 4)->sar_low = pcie->src + (64 * 1024 * 4);
	(ll + 4)->dar_low = (pcie->dst + (64 * 1024 * 4)) & 0xFFFFFFFF;
	(ll + 4)->dar_high = ((pcie->dst + (64 * 1024 * 4)) >> 32) & 0xFFFFFFFF;
	get_random_bytes((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 4),
			 64 * 1024);

	memset((ll + 5), 0x0, sizeof(struct dma_ll));
	(ll + 5)->ele_1.llp = 1;
	(ll + 5)->ele_1.tcb = 1;

	memset(&tx, 0x0, sizeof(struct dma_tx));
	tx.src = pcie->src;
	tx.channel = pcie->channel;
	tx.ll = 1;
	ret = dma_write(pcie, &tx);
	if (ret < 0) {
		dev_err(pcie->dev, "DMA-Write-LL FAILED\n");
		ret = -EIO;
		goto err_out;
	}

	/* compare copied data */
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 1)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 1),  64 * 1024)) {
		dev_err(pcie->dev, "DMA-Write-LL Chunk-1 FAILED\n");
		goto err_out;
	}
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 2)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 2), 64 * 1024)) {
		dev_err(pcie->dev, "DMA-Write-LL Chunk-2 FAILED\n");
		goto err_out;
	}
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 4)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 4),  64 * 1024)) {
		dev_err(pcie->dev, "DMA-Write-LL Chunk-3 FAILED\n");
		goto err_out;
	}
	dev_err(pcie->dev, "DMA-Write-LL PASSED\n");

err_out:
	iounmap(dst_cpu_virt);
	return ret;
}

static int read(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct dma_tx tx;
	int ret = 0;
	void __iomem *dst_cpu_virt;

	memset(&tx, 0x0, sizeof(struct dma_tx));
	tx.src = pcie->src;
	tx.dst = pcie->dst;
	tx.size = pcie->size;
	tx.channel = pcie->channel;

	dst_cpu_virt = ioremap_nocache(pcie->src, pcie->size);
	/* fill source with random data */
	get_random_bytes(dst_cpu_virt, pcie->size);

	ret = dma_read(pcie, &tx);
	if (ret < 0) {
		dev_err(pcie->dev, "DMA-Read test FAILED\n");
		ret = -EIO;
		goto err_out;
	}

	/* compare copied data */
	if (!memcmp(dst_cpu_virt, pcie->cpu_virt_addr, pcie->size))
		dev_info(pcie->dev, "DMA-Read test PASSED\n");
	else
		dev_info(pcie->dev, "DMA-Read test FAILED\n");

err_out:
	iounmap(dst_cpu_virt);
	return ret;
}

static int read_ll(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct dma_tx tx;
	int ret = 0;
	struct dma_ll *ll;
	void __iomem *dst_cpu_virt;

	dst_cpu_virt = ioremap_nocache(pcie->src, 6 * 64 * 1024);

	/* create linked list to be sent to ep's local memory */
	ll = (struct dma_ll *)(pcie->cpu_virt_addr);

	/* leave first 64K for LL element preparation */
	memset((ll + 0), 0x0, sizeof(struct dma_ll));
	(ll + 0)->size = (64 * 1024);
	(ll + 0)->sar_low = pcie->src + (64 * 1024 * 1);
	(ll + 0)->sar_high = ((pcie->src + (64 * 1024 * 1)) >> 32) & 0xFFFFFFFF;
	(ll + 0)->dar_low = pcie->dst + (64 * 1024 * 1);
	get_random_bytes((u8 *)dst_cpu_virt + (64 * 1024 * 1), 64 * 1024);

	memset((ll + 1), 0x0, sizeof(struct dma_ll));
	(ll + 1)->size = (64 * 1024);
	(ll + 1)->sar_low = pcie->src + (64 * 1024 * 2);
	(ll + 1)->sar_high = ((pcie->src + (64 * 1024 * 2)) >> 32) & 0xFFFFFFFF;
	(ll + 1)->dar_low = pcie->dst + (64 * 1024 * 2);
	get_random_bytes((u8 *)dst_cpu_virt + (64 * 1024 * 2), 64 * 1024);

	memset((ll + 2), 0x0, sizeof(struct dma_ll));
	(ll + 2)->ele_1.llp = 1;
	(ll + 2)->sar_low = (4 * sizeof(struct dma_ll)) + pcie->dst;

	memset((ll + 4), 0x0, sizeof(struct dma_ll));
	(ll + 4)->ele_1.lie = 1;
	(ll + 4)->size = (64 * 1024);
	(ll + 4)->sar_low = pcie->src + (64 * 1024 * 4);
	(ll + 4)->sar_high = ((pcie->src + (64 * 1024 * 4)) >> 32) & 0xFFFFFFFF;
	(ll + 4)->dar_low = pcie->dst + (64 * 1024 * 4);
	get_random_bytes((u8 *)dst_cpu_virt + (64 * 1024 * 4), 64 * 1024);

	memset((ll + 5), 0x0, sizeof(struct dma_ll));
	(ll + 5)->ele_1.llp = 1;
	(ll + 5)->ele_1.tcb = 1;

	memset(&tx, 0x0, sizeof(struct dma_tx));
	tx.src = pcie->dst;
	tx.channel = pcie->channel;
	tx.ll = 1;
	ret = dma_read(pcie, &tx);
	if (ret < 0) {
		dev_err(pcie->dev, "DMA-Read-LL FAILED\n");
		ret = -EIO;
		goto err_out;
	}

	/* compare copied data */
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 1)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 1), 64 * 1024)) {
		dev_err(pcie->dev, "DMA-Read-LL Chunk-1 FAILED\n");
		goto err_out;
	}
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 2)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 2), 64 * 1024)) {
		dev_err(pcie->dev, "DMA-Read-LL Chunk-2 FAILED\n");
		goto err_out;
	}
	if (memcmp((void *)((u8 *)pcie->cpu_virt_addr + (64 * 1024 * 4)),
		   (u8 *)dst_cpu_virt + (64 * 1024 * 4), 64 * 1024)) {
		dev_err(pcie->dev, "DMA-Read-LL Chunk-3 FAILED\n");
		goto err_out;
	}
	dev_err(pcie->dev, "DMA-Read-LL PASSED\n");

err_out:
	iounmap(dst_cpu_virt);
	return ret;
}
#endif

static void config_plat_gpio(struct tegra_pcie_dw *pcie, bool flag)
{
	int count;

	for (count = 0; count < pcie->n_gpios; ++count)
		gpiod_set_value(gpio_to_desc(pcie->gpios[count]), flag);

	/*
	 * According to PCI Express Card Electromechanical Specification
	 * Revision 1.1, Table-2.4, T_PVPERL (Power stable to PERST# inactive)
	 * should be a minimum of 100ms.
	 */
	if (flag && pcie->n_gpios > 0)
		msleep(100);
}

static int apply_speed_change(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	unsigned long start_jiffies;
	u32 val = 0;

	if (pcie->target_speed > (PCI_EXP_LNKSTA_CLS_8_0GB + 1)) {
		seq_puts(s, "Invalid target speed. Should be 1 ~ 4\n");
		return 0;
	}

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	if (((val >> 16) & PCI_EXP_LNKSTA_CLS) == pcie->target_speed) {
		seq_puts(s, "Link speed is already the target speed...!\n");
		return 0;
	}

	if (!tegra_platform_is_fpga() && (pcie->target_speed == 4)) {
		u32 temp1 = 0, temp2 = 0;

		dw_pcie_read(pcie->pci.dbi_base + pcie->cap_pl16g_status, 4,
			     &val);
		dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4,
			     &temp1);
		dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL_2, 4,
			     &temp2);

		if (!((val & CAP_PL16G_STATUS_REG_EQ_16G_CPL) ||
		      (((temp1 & (PCI_EXP_LNKSTA_CLS << 16)) ==
			 (PCI_EXP_LNKSTA_CLS_8_0GB << 16)) &&
			 temp2 & CFG_LINK_STATUS_CONTROL_2_PCIE_CAP_EQ_CPL))) {
			seq_puts(s, "Gen-3/4 Equalization is not complete\n");
			return 0;
		}
	}

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL_2, 4, &val);
	val &= ~PCI_EXP_LNKSTA_CLS;
	val |= pcie->target_speed;
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL_2, 4, val);

	/* Wait for previous link training to complete */
	start_jiffies = jiffies;
	for (;;) {
		dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4,
			     &val);
		if (!(val & CFG_LINK_STATUS_LT))
			break;
		if (time_after(jiffies, start_jiffies +
		    msecs_to_jiffies(1000))) {
			seq_puts(s, "Link Retrain Timeout\n");
			break;
		}
		usleep_range(1000, 1100);
	}
	if (val & CFG_LINK_STATUS_LT) {
		seq_puts(s, "Previous link training didn't complete\n");
		return 0;
	}

	/* Clear BW Management Status */
	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	val |= CFG_LINK_STATUS_BW_MAN_STATUS;
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, val);

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	val |= CFG_LINK_CONTROL_LT;
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, val);

	/* Wait for link training end. Break out after waiting for timeout */
	start_jiffies = jiffies;
	for (;;) {
		dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4,
			     &val);
		if (val & CFG_LINK_STATUS_BW_MAN_STATUS)
			break;
		if (time_after(jiffies, start_jiffies +
		     msecs_to_jiffies(1000))) {
			seq_puts(s, "Bandwidth Management Status Timeout\n");
			break;
		}
		usleep_range(1000, 1100);
	}

	/* Give 20ms time for new link status to appear in LnkSta register */
	msleep(20);

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	if (((val >> 16) & PCI_EXP_LNKSTA_CLS) == pcie->target_speed) {
		seq_puts(s, "Link speed is successful...!\n");
	} else {
		seq_puts(s, "Link speed change failed...");
		seq_printf(s, "Settled for Gen-%u\n", (val >> 16) &
			   PCI_EXP_LNKSTA_CLS);
	}

	return 0;
}

static int apply_pme_turnoff(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);

	tegra_pcie_downstream_dev_to_D0(pcie);

	if (!tegra_pcie_dw_pme_turnoff(pcie))
		seq_puts(s, "PME_TurnOff sent and Link is in L2 state\n");
	else
		seq_puts(s, "PME_TurnOff failed\n");

	return 0;
}

static int apply_sbr(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + PCI_BRIDGE_CONTROL, 2, &val);
	val |= PCI_BRIDGE_CTL_BUS_RESET;
	dw_pcie_write(pcie->pci.dbi_base + PCI_BRIDGE_CONTROL, 2, val);
	mdelay(1);
	dw_pcie_read(pcie->pci.dbi_base + PCI_BRIDGE_CONTROL, 2, &val);
	val &= ~PCI_BRIDGE_CTL_BUS_RESET;
	dw_pcie_write(pcie->pci.dbi_base + PCI_BRIDGE_CONTROL, 2, val);

	seq_puts(s, "Secondary Bus Reset applied successfully...\n");

	return 0;
}

static inline u32 event_counter_prog(struct tegra_pcie_dw *pcie, u32 event)
{
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + pcie->event_cntr_ctrl, 4, &val);
	val &= ~(EVENT_COUNTER_EVENT_SEL_MASK << EVENT_COUNTER_EVENT_SEL_SHIFT);
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	val |= event << EVENT_COUNTER_EVENT_SEL_SHIFT;
	val |= EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	dw_pcie_write(pcie->pci.dbi_base + pcie->event_cntr_ctrl, 4, val);
	dw_pcie_read(pcie->pci.dbi_base + pcie->event_cntr_data, 4, &val);
	return val;
}

static int aspm_state_cnt(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	u32 val = 0;

	seq_printf(s, "Tx L0s entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_Tx_L0S));

	seq_printf(s, "Rx L0s entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_Rx_L0S));

	seq_printf(s, "Link L1 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1));

	seq_printf(s, "Link L1.1 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1_1));

	seq_printf(s, "Link L1.2 entry count : %u\n",
		   event_counter_prog(pcie, EVENT_COUNTER_EVENT_L1_2));

	/* Clear all counters */
	dw_pcie_write(pcie->pci.dbi_base + pcie->event_cntr_ctrl, 4,
		      EVENT_COUNTER_ALL_CLEAR);

	/* Re-enable counting */
	val = EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	dw_pcie_write(pcie->pci.dbi_base + pcie->event_cntr_ctrl, 4, val);

	return 0;
}

#ifdef CONFIG_PCIE_TEGRA_DW_LANE_MARGIN
static void setup_margin_cmd(struct tegra_pcie_dw *pcie, enum margin_cmds mcmd,
			     int rcv_no, int payload)
{
	switch (mcmd) {
	case MARGIN_SET_ERR_COUNT:
		pcie->mcmd.margin_type = 2;
		pcie->mcmd.rxm_payload_check = 1;
		break;
	case MARGIN_SET_NO_CMD:
		pcie->mcmd.margin_type = 7;
		pcie->mcmd.rxm_payload_check = 1;
		break;
	case MARGIN_SET_X_OFFSET:
		pcie->mcmd.margin_type = 3;
		pcie->mcmd.rxm_payload_check = 0;
		break;
	case MARGIN_SET_Y_OFFSET:
		pcie->mcmd.margin_type = 4;
		pcie->mcmd.rxm_payload_check = 0;
		break;
	case MARGIN_SET_NORMAL:
		pcie->mcmd.margin_type = 2;
		pcie->mcmd.rxm_payload_check = 1;
		break;
	case MARGIN_CLR_ERR:
		pcie->mcmd.margin_type = 2;
		pcie->mcmd.rxm_payload_check = 1;
		break;
	}
	pcie->mcmd.rcv_no = rcv_no;
	pcie->mcmd.payload = payload;
	pcie->mcmd.rxm_cmd_check = 1;
}


static void issue_margin_cmd(struct tegra_pcie_dw *pcie, struct pci_dev *pdev)
{
	u16 val;
	int i, offset, pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_MARGIN);
	for (i = 0; i < pcie->init_link_width; i++) {
		offset = pos + PCI_MARGIN_LANE_CTRL + 4 * i;
		pci_read_config_word(pdev, offset, &val);
		val &= ~PCI_MARGIN_LANE_CTRL_RCV_NUM_MASK;
		val |= pcie->mcmd.rcv_no;
		val &= ~PCI_MARGIN_LANE_CTRL_TYPE_MASK;
		val |= (pcie->mcmd.margin_type <<
			PCI_MARGIN_LANE_CTRL_TYPE_SHIFT);
		val &= ~PCI_MARGIN_LANE_CTRL_PAYLOAD_MASK;
		val |= (pcie->mcmd.payload <<
			PCI_MARGIN_LANE_CTRL_PAYLOAD_SHIFT);
		pci_write_config_word(pdev, offset, val);
	}
}

static void read_margin_status(struct tegra_pcie_dw *pcie, struct seq_file *s,
			       struct pci_dev *pdev, int step, char side)
{
	u16 val;
	int pos, offset, rcv_no, margin_type, payload, i;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_MARGIN);
	for (i = 0; i < pcie->init_link_width; i++) {
		offset = pos + PCI_MARGIN_LANE_STATUS + 4 * i;
		pci_read_config_word(pdev, offset, &val);
		rcv_no = val & PCI_MARGIN_LANE_STATUS_RCV_NUM_MASK;
		margin_type = (val & PCI_MARGIN_LANE_STATUS_TYPE_MASK)
				>> PCI_MARGIN_LANE_STATUS_TYPE_SHIFT;
		payload = (val & PCI_MARGIN_LANE_STATUS_PAYLOAD_MASK)
			>> PCI_MARGIN_LANE_STATUS_PAYLOAD_SHIFT;
		if (pcie->mcmd.rxm_cmd_check) {
			if (pcie->mcmd.rcv_no != rcv_no)
				seq_printf(s, "Rcv no. check fail: rcv_no=%d "
					   "status rcv_no=%d\n",
					   pcie->mcmd.rcv_no, rcv_no);
			if (pcie->mcmd.margin_type != margin_type)
				seq_printf(s, "Margin type check fail: type=%d "
					   "status type=%d\n",
					   pcie->mcmd.margin_type, margin_type);
		}
		if (pcie->mcmd.rxm_payload_check) {
			if (pcie->mcmd.payload != payload)
				seq_printf(s, "Payload check fail: payload=%d "
					   "status payload=%d\n",
					   pcie->mcmd.payload, payload);
		}
		if ((margin_type == 3) || (margin_type == 4))
			dev_info(&pdev->bus->dev, "Lane=%d Side=%c Step=%d Error=0x%x\n",
				 i, side, step, (payload & 0x3f));
	}
}

static int verify_timing_margin(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct pcie_port *pp = &pcie->pp;
	struct pci_dev *ppdev, *pdev;
	u32 val = 0;
	u16 value;
	int i = 0, pos;

	ppdev = pci_get_slot(pp->bus, PCI_DEVFN(0, 0));
	pci_dev_put(ppdev);
	pdev = pci_get_slot(ppdev->subordinate, PCI_DEVFN(0, 0));
	pci_dev_put(pdev);

	pcie_capability_read_word(ppdev, PCI_EXP_LNKSTA, &value);
	if ((value & PCI_EXP_LNKSTA_CLS) != 0x4) {
		seq_puts(s, "Link is not in Gen4\n");
		return 0;
	}

	pos = pci_find_ext_capability(ppdev, PCI_EXT_CAP_ID_MARGIN);
	if (!pos) {
		seq_puts(s, "Lane margining is not defined in RP\n");
		goto endpoint;
	}

	pci_read_config_word(ppdev, pos + PCI_MARGIN_PORT_STATUS, &value);
	if (!(value & PCI_MARGIN_PORT_STATUS_SW_READY) &&
	    !(value & PCI_MARGIN_PORT_STATUS_READY)) {
		seq_puts(s, "Lane margining is not ready in RP\n");
		goto endpoint;
	}

	setup_margin_cmd(pcie, MARGIN_SET_ERR_COUNT, RP_RCV_NO,
			 MAX_ERR_CNT_PAYLOAD);
	issue_margin_cmd(pcie, ppdev);
	msleep(MARGIN_READ_DELAY);
	read_margin_status(pcie, s, ppdev, i, NO_STEP);

#ifdef CONFIG_PCIE_TEGRA_DW_TWO_SIDE_LANE_MARGIN
	for (i = 1; i <= NUM_TIMING_STEPS; i++) {
		/*
		 * Step Margin to timing offset to left of default
		 * payload = offset | (0x10 << 6)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_X_OFFSET, RP_RCV_NO,
				 i | LEFT_STEP_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, ppdev, i, LEFT_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, RP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, RP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);
	}
#endif

	for (i = 1; i <= NUM_TIMING_STEPS; i++) {
		/*
		 * Step Margin to timing offset to right of default
		 * payload = offset | (0x00 << 6)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_X_OFFSET, RP_RCV_NO,
				 i | RIGHT_STEP_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, ppdev, i, RIGHT_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, RP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, RP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);
	}

endpoint:
	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_MARGIN);
	if (!pos) {
		seq_puts(s, "Lane margining is not defined in EP\n");
		goto end;
	}

	pci_read_config_word(pdev, pos + PCI_MARGIN_PORT_STATUS, &value);
	if (!(value & PCI_MARGIN_PORT_STATUS_SW_READY) &&
	    !(value & PCI_MARGIN_PORT_STATUS_READY)) {
		seq_puts(s, "Lane margining is not ready in EP\n");
		goto end;
	}

	setup_margin_cmd(pcie, MARGIN_SET_ERR_COUNT, EP_RCV_NO,
			 MAX_ERR_CNT_PAYLOAD);
	issue_margin_cmd(pcie, pdev);
	msleep(MARGIN_READ_DELAY);
	read_margin_status(pcie, s, pdev, i, NO_STEP);

#ifdef CONFIG_PCIE_TEGRA_DW_TWO_SIDE_LANE_MARGIN
	for (i = 1; i <= NUM_TIMING_STEPS; i++) {
		/*
		 * Step Margin to timing offset to left of default
		 * payload = offset | (0x10 << 6)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_X_OFFSET, EP_RCV_NO,
				 i | LEFT_STEP_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, pdev, i, LEFT_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, EP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, EP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);
	}
#endif

	for (i = 1; i <= NUM_TIMING_STEPS; i++) {
		/*
		 * Step Margin to timing offset to right of default
		 * payload = offset | (0x00 << 6)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_X_OFFSET, EP_RCV_NO,
				 i | RIGHT_STEP_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, pdev, i, RIGHT_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, EP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, EP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);
	}

end:
	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &value);
	if ((value & PCI_EXP_LNKSTA_CLS) != 0x4)
		seq_puts(s, "Link is not in Gen4, restart the device & execute lane margin\n");

	val = readl(pcie->appl_base + APPL_DEBUG);
	val &= APPL_DEBUG_LTSSM_STATE_MASK;
	val >>= APPL_DEBUG_LTSSM_STATE_SHIFT;

	if (val != APPL_DEBUG_PM_LINKST_IN_L0)
		seq_puts(s, "Link is not in L0, restart the device & execute lane margin\n");

	return 0;
}

static int verify_voltage_margin(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);
	struct pcie_port *pp = &pcie->pp;
	struct pci_dev *ppdev, *pdev;
	u32 val = 0;
	u16 value = 0;
	int i = 0, pos = 0;

	ppdev = pci_get_slot(pp->bus, PCI_DEVFN(0, 0));
	pci_dev_put(ppdev);
	pdev = pci_get_slot(ppdev->subordinate, PCI_DEVFN(0, 0));
	pci_dev_put(pdev);

	pcie_capability_read_word(ppdev, PCI_EXP_LNKSTA, &value);
	if ((value & PCI_EXP_LNKSTA_CLS) != 0x4) {
		seq_puts(s, "Link is not in Gen4\n");
		return 0;
	}

	pos = pci_find_ext_capability(ppdev, PCI_EXT_CAP_ID_MARGIN);
	if (!pos) {
		seq_puts(s, "Lane margining is not defined in RP\n");
		goto endpoint;
	}

	pci_read_config_word(ppdev, pos + PCI_MARGIN_PORT_STATUS, &value);
	if (!(value & PCI_MARGIN_PORT_STATUS_SW_READY) &&
	    !(value & PCI_MARGIN_PORT_STATUS_READY)) {
		seq_puts(s, "Lane margining is not ready in RP\n");
		goto endpoint;
	}

	setup_margin_cmd(pcie, MARGIN_SET_ERR_COUNT, RP_RCV_NO,
			 MAX_ERR_CNT_PAYLOAD);
	issue_margin_cmd(pcie, ppdev);
	msleep(MARGIN_READ_DELAY);
	read_margin_status(pcie, s, ppdev, i, NO_STEP);

#ifdef CONFIG_PCIE_TEGRA_DW_TWO_SIDE_LANE_MARGIN
	for (i = 1; i <= NUM_VOLTAGE_STEPS; i++) {
		/*
		 * Step Margin to voltage offset to down of default
		 * payload = offset | (0x01 << 7)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_Y_OFFSET, RP_RCV_NO,
				 i | DOWN_STEP_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, ppdev, i, DOWN_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, RP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, RP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);
	}
#endif

	for (i = 1; i <= NUM_VOLTAGE_STEPS; i++) {
		/*
		 * Step Margin to voltage offset to up of default
		 * payload = offset | (0x00 << 7)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_Y_OFFSET, RP_RCV_NO,
				 i | UP_STEP_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, ppdev, i, UP_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, RP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, RP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, ppdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, ppdev, i, NO_STEP);
	}

endpoint:
	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_MARGIN);
	if (!pos) {
		seq_puts(s, "Lane margining is not defined in RP\n");
		goto end;
	}

	pci_read_config_word(pdev, pos + PCI_MARGIN_PORT_STATUS, &value);
	if (!(value & PCI_MARGIN_PORT_STATUS_SW_READY) &&
	    !(value & PCI_MARGIN_PORT_STATUS_READY)) {
		seq_puts(s, "Lane margining is not ready in RP\n");
		goto end;
	}

	setup_margin_cmd(pcie, MARGIN_SET_ERR_COUNT, EP_RCV_NO,
			 MAX_ERR_CNT_PAYLOAD);
	issue_margin_cmd(pcie, pdev);
	msleep(MARGIN_READ_DELAY);
	read_margin_status(pcie, s, pdev, i, NO_STEP);

#ifdef CONFIG_PCIE_TEGRA_DW_TWO_SIDE_LANE_MARGIN
	for (i = 1; i <= NUM_VOLTAGE_STEPS; i++) {
		/*
		 * Step Margin to voltage offset to down of default
		 * payload = offset | (0x01 << 7)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_Y_OFFSET, EP_RCV_NO,
				 i | DOWN_STEP_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, pdev, i, DOWN_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, EP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, EP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);
	}
#endif

	for (i = 1; i <= NUM_VOLTAGE_STEPS; i++) {
		/*
		 * Step Margin to voltage offset to up of default
		 * payload = offset | (0x00 << 7)
		 */
		setup_margin_cmd(pcie, MARGIN_SET_Y_OFFSET, EP_RCV_NO,
				 i | UP_STEP_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_WIN_TIME);
		read_margin_status(pcie, s, pdev, i, UP_STEP);

		setup_margin_cmd(pcie, MARGIN_SET_NORMAL, EP_RCV_NO,
				 NORMAL_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);

		setup_margin_cmd(pcie, MARGIN_CLR_ERR, EP_RCV_NO,
				 CLR_ERR_PAYLOAD);
		issue_margin_cmd(pcie, pdev);
		msleep(MARGIN_READ_DELAY);
		read_margin_status(pcie, s, pdev, i, NO_STEP);
	}

end:
	pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &value);
	if ((value & PCI_EXP_LNKSTA_CLS) != 0x4)
		seq_puts(s, "Link is not in Gen4, restart the device & execute lane margin\n");

	val = readl(pcie->appl_base + APPL_DEBUG);
	val &= APPL_DEBUG_LTSSM_STATE_MASK;
	val >>= APPL_DEBUG_LTSSM_STATE_SHIFT;

	if (val != APPL_DEBUG_PM_LINKST_IN_L0)
		seq_puts(s, "Link is not in L0, restart the device & execute lane margin\n");

	return 0;
}
#endif

static int __attach_controller(struct tegra_pcie_dw *pcie)
{
	int ret;

	if (!pcie->link_state && !pcie->power_down_en) {
		ret = pm_runtime_put_sync(pcie->dev);
		if (ret)
			return ret;
	}

	ret = pm_runtime_get_sync(pcie->dev);
	if (ret)
		return ret;
	pcie->link_state = tegra_pcie_dw_link_up(&pcie->pci);

	return 0;
}

static int __detach_controller(struct tegra_pcie_dw *pcie)
{
	if (!pcie->link_state && pcie->power_down_en)
		return 0;

	return pm_runtime_put_sync(pcie->dev);
}

/* Enables root port controller and attempts PCIe link up with the device
 * connected downstream. If link is up, registers host controller with
 * PCIe sub-system.
 * @cookie : opaque pointer returned by tegra_pcie_detach_controller() API
 */
int tegra_pcie_attach_controller(void *cookie)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)cookie;

	return __attach_controller(pcie);
}
EXPORT_SYMBOL(tegra_pcie_attach_controller);

/* Removes PCIe hierarchy of the respective host controller and brings PCIe
 * link down in a safe way
 * @pdev: pointer to end point's pci_dev structure
 * returns a cookie which needs to be passed to
 * tegra_pcie_attach_controller() API
 */
void *tegra_pcie_detach_controller(struct pci_dev *pdev)
{
	struct pcie_port *pp = pdev->sysdata;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	int ret;

	ret = __detach_controller(pcie);
	if (ret)
		return ERR_PTR(ret);

	return (void *)pcie;
}
EXPORT_SYMBOL(tegra_pcie_detach_controller);

static int hot_plug(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);

	return __attach_controller(pcie);
}

static int hot_unplug(struct seq_file *s, void *data)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)(s->private);

	return __detach_controller(pcie);
}

#define DEFINE_ENTRY(__name)	\
static int __name ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __name, inode->i_private); \
}									\
static const struct file_operations __name ## _fops = {	\
	.open		= __name ## _open,	\
	.read		= seq_read,	\
	.llseek		= seq_lseek,	\
	.release	= single_release,	\
}

/* common */
#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
DEFINE_ENTRY(write);
DEFINE_ENTRY(write_ll);
DEFINE_ENTRY(read);
DEFINE_ENTRY(read_ll);
#endif
DEFINE_ENTRY(apply_speed_change);
DEFINE_ENTRY(apply_pme_turnoff);
DEFINE_ENTRY(apply_sbr);
DEFINE_ENTRY(aspm_state_cnt);
#ifdef CONFIG_PCIE_TEGRA_DW_LANE_MARGIN
DEFINE_ENTRY(verify_timing_margin);
DEFINE_ENTRY(verify_voltage_margin);
#endif
DEFINE_ENTRY(hot_plug);
DEFINE_ENTRY(hot_unplug);

#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
static void init_dma_test_debugfs(struct tegra_pcie_dw *pcie)
{
	struct dentry *d;
	int i;

	for (i = 0; i < DMA_WR_CHNL_NUM; i++) {
		mutex_init(&pcie->wr_lock[i]);
		init_completion(&pcie->wr_cpl[i]);
	}

	for (i = 0; i < DMA_RD_CHNL_NUM; i++) {
		mutex_init(&pcie->rd_lock[i]);
		init_completion(&pcie->rd_cpl[i]);
	}

	/* alloc memory required for RP-DMA testing */
	pcie->cpu_virt_addr = dma_alloc_coherent(pcie->dev, DMA_TEST_BUF_SIZE,
					    &pcie->dma_addr, GFP_KERNEL);
	if (!pcie->cpu_virt_addr) {
		dev_err(pcie->dev,
			"Memory allocation for DMA failed...! exiting...!");
		return;
	}
	dev_info(pcie->dev,
		 "---> Allocated memory for DMA @ 0x%llX\n", pcie->dma_addr);

	d = debugfs_create_x64("src", 0644, pcie->debugfs, &pcie->src);
	if (!d)
		dev_err(pcie->dev, "debugfs for src addr failed\n");

	d = debugfs_create_x64("dst", 0644, pcie->debugfs, &pcie->dst);
	if (!d)
		dev_err(pcie->dev, "debugfs for dst addr failed\n");

	d = debugfs_create_x32("size", 0644, pcie->debugfs, &pcie->size);
	if (!d)
		dev_err(pcie->dev, "debugfs for size failed\n");

	d = debugfs_create_x8("channel", 0644, pcie->debugfs, &pcie->channel);
	if (!d)
		dev_err(pcie->dev, "debugfs for channel failed\n");

	d = debugfs_create_file("write", 0444, pcie->debugfs, (void *)pcie,
				&write_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for write failed\n");

	d = debugfs_create_file("write_ll", 0444, pcie->debugfs, (void *)pcie,
				&write_ll_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for write failed\n");

	d = debugfs_create_file("read", 0444, pcie->debugfs, (void *)pcie,
				&read_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for read failed\n");

	d = debugfs_create_file("read_ll", 0444, pcie->debugfs, (void *)pcie,
				&read_ll_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for read failed\n");
}

static void destroy_dma_test_debugfs(struct tegra_pcie_dw *pcie)
{
	int i;

	dma_free_coherent(pcie->dev, DMA_TEST_BUF_SIZE, pcie->cpu_virt_addr,
			  pcie->dma_addr);

	for (i = 0; i < DMA_WR_CHNL_NUM; i++)
		mutex_destroy(&pcie->wr_lock[i]);

	for (i = 0; i < DMA_RD_CHNL_NUM; i++)
		mutex_destroy(&pcie->rd_lock[i]);
}
#else
static void init_dma_test_debugfs(struct tegra_pcie_dw *pcie)
{
}

static void destroy_dma_test_debugfs(struct tegra_pcie_dw *pcie)
{
}
#endif

static int init_debugfs(struct tegra_pcie_dw *pcie)
{
	struct dentry *d;

	d = debugfs_create_u32("target_speed", 0644, pcie->debugfs,
			       &pcie->target_speed);
	if (!d)
		dev_err(pcie->dev, "debugfs for target_speed failed\n");

	d = debugfs_create_file("apply_speed_change", 0444, pcie->debugfs,
				(void *)pcie, &apply_speed_change_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for apply_speed_change failed\n");

	d = debugfs_create_file("apply_pme_turnoff", 0444, pcie->debugfs,
				(void *)pcie, &apply_pme_turnoff_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for apply_pme_turnoff failed\n");

	d = debugfs_create_file("apply_sbr", 0444, pcie->debugfs,
				(void *)pcie, &apply_sbr_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for apply_sbr failed\n");

	d = debugfs_create_file("aspm_state_cnt", 0444, pcie->debugfs,
				(void *)pcie, &aspm_state_cnt_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for aspm_state_cnt failed\n");

#ifdef CONFIG_PCIE_TEGRA_DW_LANE_MARGIN
	d = debugfs_create_file("verify_timing_margin", 0444, pcie->debugfs,
				(void *)pcie, &verify_timing_margin_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for verify_timing_margin failed\n");

	d = debugfs_create_file("verify_voltage_margin", 0444, pcie->debugfs,
				(void *)pcie, &verify_voltage_margin_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for verify_voltage_margin failed\n");
#endif

	d = debugfs_create_file("hot_plug", 0444, pcie->debugfs,
				(void *)pcie, &hot_plug_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for hot_plug failed\n");

	d = debugfs_create_file("hot_unplug", 0444, pcie->debugfs,
				(void *)pcie, &hot_unplug_fops);
	if (!d)
		dev_err(pcie->dev, "debugfs for hot_unplug failed\n");

	init_dma_test_debugfs(pcie);

	return 0;
}

static int tegra_pcie_dw_wr_own_conf(struct pcie_port *pp, int where, int size,
				     u32 val)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/* This is EP specific register and system hangs when it is
	 * accessed with link being in ASPM-L1 state.
	 * So skip accessing it altogether
	 */
	if (where == PORT_LOGIC_MSIX_DOORBELL)
		return PCIBIOS_SUCCESSFUL;
	else
		return dw_pcie_write(pci->dbi_base + where, size, val);
}

static int tegra_pcie_dw_wr_other_conf(struct pcie_port *pp,
				       struct pci_bus *bus, unsigned int devfn,
				       int where, int size, u32 val)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->parent->number == pp->root_bus_nr)
		type = PCIE_ATU_TYPE_CFG0;
	else
		type = PCIE_ATU_TYPE_CFG1;

	cpu_addr = pp->cfg1_base;
	cfg_size = pp->cfg1_size;
	va_cfg_base = pp->va_cfg1_base;

	outbound_atu(pp, PCIE_ATU_REGION_INDEX0, type, cpu_addr, busdev,
		     cfg_size);
	ret = dw_pcie_write(va_cfg_base + where, size, val);

	return ret;
}

static void tegra_pcie_enable_system_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	u32 val;

	val = readl(pcie->appl_base + APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_LINK_STATE_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L0_0);

	val = readl(pcie->appl_base + APPL_INTR_EN_L1_0_0);
	val |= APPL_INTR_EN_L1_0_0_LINK_REQ_RST_NOT_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L1_0_0);

	if (pcie->cdm_check) {
		val = readl(pcie->appl_base + APPL_INTR_EN_L0_0);
		val |= APPL_INTR_EN_L0_0_CDM_REG_CHK_INT_EN;
		writel(val, pcie->appl_base + APPL_INTR_EN_L0_0);

		val = readl(pcie->appl_base + APPL_INTR_EN_L1_18);
		val |= APPL_INTR_EN_L1_18_CDM_REG_CHK_CMP_ERR;
		val |= APPL_INTR_EN_L1_18_CDM_REG_CHK_LOGIC_ERR;
		writel(val, pcie->appl_base + APPL_INTR_EN_L1_18);
	}

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 4, &val);
	pcie->init_link_width = ((val >> 16) & PCI_EXP_LNKSTA_NLW) >>
				PCI_EXP_LNKSTA_NLW_SHIFT;
	val |= PCI_EXP_LNKCTL_LBMIE;
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_STATUS_CONTROL, 2, val);
}

static void tegra_pcie_enable_legacy_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	u32 val;

	/* enable legacy interrupt generation */
	val = readl(pcie->appl_base + APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_INTR_EN;
	val |= APPL_INTR_EN_L0_0_INT_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L0_0);

	val = readl(pcie->appl_base + APPL_INTR_EN_L1_8_0);
	val |= APPL_INTR_EN_L1_8_INTX_EN;
	val |= APPL_INTR_EN_L1_8_AUTO_BW_INT_EN;
	val |= APPL_INTR_EN_L1_8_BW_MGT_INT_EN;
	if (IS_ENABLED(CONFIG_PCIEAER))
		val |= APPL_INTR_EN_L1_8_AER_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L1_8_0);

#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
	if (!pcie->dma_poll) {
		/* Enable Interrupt for DMA completion */
		val = readl(pcie->appl_base + APPL_INTR_EN_L1_8_0);
		val |= APPL_INTR_EN_L1_8_EDMA_INT_EN;
		writel(val, pcie->appl_base + APPL_INTR_EN_L1_8_0);
	}
#endif
}

static int tegra_pcie_enable_msi_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	u32 val;
	int ret;

	ret = dw_pcie_msi_init(pp);
	if (ret)
		return ret;

	/* enable MSI interrupt generation */
	val = readl(pcie->appl_base + APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_MSI_INTR_EN;
	val |= APPL_INTR_EN_L0_0_MSI_RCV_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L0_0);
	return 0;
}

static int tegra_pcie_enable_interrupts(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);

	/* Clear interrupt statuses before enabling interrupts */
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_1);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_2);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_3);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_6);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_7);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_8_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_9);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_10);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_11);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_13);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_14);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_15);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_17);

	tegra_pcie_enable_system_interrupts(pp);
	tegra_pcie_enable_legacy_interrupts(pp);
	if (IS_ENABLED(CONFIG_PCI_MSI))
		return tegra_pcie_enable_msi_interrupts(pp);
	else
		return 0;
}

static void disable_aspm_l0s(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_CAP, 4, &val);
	val &= ~(PCI_EXP_LNKCTL_ASPM_L0S << 10);
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_CAP, 4, val);
}

static void disable_aspm_l10(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + CFG_LINK_CAP, 4, &val);
	val &= ~(PCI_EXP_LNKCTL_ASPM_L1 << 10);
	dw_pcie_write(pcie->pci.dbi_base + CFG_LINK_CAP, 4, val);
}

static void disable_aspm_l11(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, &val);
	val &= ~PCI_L1SS_CAP_ASPM_L1_1;
	dw_pcie_write(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, val);
}

static void disable_aspm_l12(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;

	dw_pcie_read(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, &val);
	val &= ~PCI_L1SS_CAP_ASPM_L1_2;
	dw_pcie_write(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, val);
}

static void program_gen3_gen4_eq_presets(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	u32 val = 0;

	val = readl(pci->dbi_base + GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	writel(val, pci->dbi_base + GEN3_RELATED_OFF);

	val = readl(pci->dbi_base + GEN3_EQ_CONTROL_OFF);
	val &= ~GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK;
	val |= (0x3ff << GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT);
	val &= ~GEN3_EQ_CONTROL_OFF_FB_MODE_MASK;
	writel(val, pci->dbi_base + GEN3_EQ_CONTROL_OFF);

	val = readl(pci->dbi_base + GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	val |= (0x1 << GEN3_RELATED_OFF_RATE_SHADOW_SEL_SHIFT);
	writel(val, pci->dbi_base + GEN3_RELATED_OFF);

	val = readl(pci->dbi_base + GEN3_EQ_CONTROL_OFF);
	val &= ~GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_MASK;
	val |= (0x360 << GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC_SHIFT);
	val &= ~GEN3_EQ_CONTROL_OFF_FB_MODE_MASK;
	writel(val, pci->dbi_base + GEN3_EQ_CONTROL_OFF);

	val = readl(pci->dbi_base + GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_RATE_SHADOW_SEL_MASK;
	writel(val, pci->dbi_base + GEN3_RELATED_OFF);
}

static void init_gen3_gen4_eq_presets(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	int i;
	u32 val;

	/* program init preset */
	if (pcie->preset_init < 11) {
		for (i = 0; i < pcie->num_lanes; i++) {
			dw_pcie_read(pci->dbi_base + CAP_SPCIE_CAP_OFF
					 + (i * 2), 2, &val);
			val &= ~CAP_SPCIE_CAP_OFF_DSP_TX_PRESET0_MASK;
			val |= pcie->preset_init;
			val &= ~CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_MASK;
			val |= (pcie->preset_init <<
				   CAP_SPCIE_CAP_OFF_USP_TX_PRESET0_SHIFT);
			dw_pcie_write(pci->dbi_base + CAP_SPCIE_CAP_OFF
					 + (i * 2), 2, val);

			dw_pcie_read(pci->dbi_base + pcie->cap_pl16g_cap_off
					 + i, 1, &val);
			val &= ~PL16G_CAP_OFF_DSP_16G_TX_PRESET_MASK;
			val |= pcie->preset_init;
			val &= ~PL16G_CAP_OFF_USP_16G_TX_PRESET_MASK;
			val |= (pcie->preset_init <<
				PL16G_CAP_OFF_USP_16G_TX_PRESET_SHIFT);
			dw_pcie_write(pci->dbi_base + pcie->cap_pl16g_cap_off
					 + i, 1, val);
		}
	}
}

static int tegra_pcie_dw_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	u32 val, tmp;
	int count = 200;

	if (tegra_platform_is_fpga()) {
		/* Program correct VID and DID on FPGA */
		dw_pcie_write(pci->dbi_base + PCI_VENDOR_ID, 2, 0x10DE);
		dw_pcie_write(pci->dbi_base + PCI_DEVICE_ID, 2, 0x1AD1);

		/* Required for L1.1 working on FPGA */
		val = readl(pcie->appl_base + APPL_GTH_PHY);
		val &= 0xFFFF0003;
		val &= ~(0x2);
		val |= 0x7F4;
		writel(val, pcie->appl_base + APPL_GTH_PHY);

		/* Program correct L0s and L1 exit latencies */
		dw_pcie_read(pci->dbi_base + CFG_LINK_CAP, 4, &tmp);
		tmp &= ~PCI_EXP_LNKCAP_L0SEL;
		tmp |= 0x4; /* 512 ns to less than 1us */
		tmp &= ~PCI_EXP_LNKCAP_L1EL;
		tmp |= 0x6; /* 32us to 64us */
		dw_pcie_write(pci->dbi_base + CFG_LINK_CAP, 4, tmp);

		dw_pcie_read(pci->dbi_base + AUX_CLK_FREQ, 4, &tmp);
		tmp &= ~(0x3FF);
		tmp |= 0x6;
		dw_pcie_write(pci->dbi_base + AUX_CLK_FREQ, 4, tmp);
	} else {
		dw_pcie_read(pci->dbi_base + AUX_CLK_FREQ, 4, &tmp);
		tmp &= ~(0x3FF);
		/* CHECK: Confirm this value for Silicon */
		tmp |= pcie->aux_clk_freq;
		dw_pcie_write(pci->dbi_base + AUX_CLK_FREQ, 4, tmp);
	}

	dw_pcie_read(pci->dbi_base + PCI_IO_BASE, 4, &tmp);
	tmp &= ~(IO_BASE_IO_DECODE | IO_BASE_IO_DECODE_BIT8);
	dw_pcie_write(pci->dbi_base + PCI_IO_BASE, 4, tmp);

	dw_pcie_read(pci->dbi_base + CFG_PREF_MEM_LIMIT_BASE, 4, &tmp);
	tmp |= CFG_PREF_MEM_LIMIT_BASE_MEM_DECODE;
	tmp |= CFG_PREF_MEM_LIMIT_BASE_MEM_LIMIT_DECODE;
	dw_pcie_write(pci->dbi_base + CFG_PREF_MEM_LIMIT_BASE, 4, tmp);

	/* Configure FTS */
	dw_pcie_read(pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL, 4, &tmp);
	tmp &= ~(N_FTS_MASK << N_FTS_SHIFT);
	tmp |= N_FTS_VAL << N_FTS_SHIFT;
	dw_pcie_write(pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL, 4, tmp);

	dw_pcie_read(pci->dbi_base + PORT_LOGIC_GEN2_CTRL, 4, &tmp);
	tmp &= ~FTS_MASK;
	tmp |= FTS_VAL;
	dw_pcie_write(pci->dbi_base + PORT_LOGIC_GEN2_CTRL, 4, tmp);

	/* Enable 0xFFFF0001 response for CRS */
	dw_pcie_read(pci->dbi_base + PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT, 4,
		     &tmp);
	tmp &= ~(AMBA_ERROR_RESPONSE_CRS_MASK << AMBA_ERROR_RESPONSE_CRS_SHIFT);
	tmp |= (AMBA_ERROR_RESPONSE_CRS_OKAY_FFFF0001 <<
		AMBA_ERROR_RESPONSE_CRS_SHIFT);
	dw_pcie_write(pci->dbi_base + PORT_LOGIC_AMBA_ERROR_RESPONSE_DEFAULT, 4,
		      tmp);

	/* Set MPS to 256 in DEV_CTL */
	dw_pcie_read(pci->dbi_base + CFG_DEV_STATUS_CONTROL, 4, &tmp);
	tmp &= ~CFG_DEV_STATUS_CONTROL_MPS_MASK;
	tmp |= 1 << CFG_DEV_STATUS_CONTROL_MPS_SHIFT;
	dw_pcie_write(pci->dbi_base + CFG_DEV_STATUS_CONTROL, 4, tmp);

	/* Configure Max Speed from DT */
	dw_pcie_read(pci->dbi_base + CFG_LINK_CAP, 4, &tmp);
	tmp &= ~CFG_LINK_CAP_MAX_LINK_SPEED_MASK;
	tmp |= pcie->max_speed;
	dw_pcie_write(pci->dbi_base + CFG_LINK_CAP, 4, tmp);
	dw_pcie_read(pci->dbi_base + CFG_LINK_STATUS_CONTROL_2, 4, &tmp);
	tmp &= ~CFG_LINK_STATUS_CONTROL_2_TARGET_LS_MASK;
	tmp |= pcie->init_speed;
	dw_pcie_write(pci->dbi_base + CFG_LINK_STATUS_CONTROL_2, 4, tmp);

	/* Configure Max lane width from DT */
	dw_pcie_read(pci->dbi_base + CFG_LINK_CAP, 4, &tmp);
	tmp &= ~CFG_LINK_CAP_MAX_WIDTH_MASK;
	tmp |= (pcie->num_lanes << CFG_LINK_CAP_MAX_WIDTH_SHIFT);
	dw_pcie_write(pci->dbi_base + CFG_LINK_CAP, 4, tmp);

	/* Enable ASPM counters */
	val = EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	dw_pcie_write(pci->dbi_base + pcie->event_cntr_ctrl, 4, val);

	init_gen3_gen4_eq_presets(pcie);
	program_gen3_gen4_eq_presets(pcie);

	/* Program T_cmrt and T_pwr_on values */
	dw_pcie_read(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, &val);
	val &= ~(PCI_L1SS_CAP_CM_RTM_MASK | PCI_L1SS_CAP_PWRN_VAL_MASK);
	val |= (pcie->aspm_cmrt << PCI_L1SS_CAP_CM_RTM_SHIFT);
	val |= (pcie->aspm_pwr_on_t << PCI_L1SS_CAP_PWRN_VAL_SHIFT);
	dw_pcie_write(pcie->pci.dbi_base + pcie->cfg_link_cap_l1sub, 4, val);

	/* Program L0s and L1 entrance latencies */
	val = readl(pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);
	val &= ~L0S_ENTRANCE_LAT_MASK;
	val |= (pcie->aspm_l0s_enter_lat << L0S_ENTRANCE_LAT_SHIFT);
	val |= ENTER_ASPM;
	writel(val, pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);

	/* Program what ASPM states sould get advertised */
	if (pcie->disabled_aspm_states & 0x1)
		disable_aspm_l0s(pcie); /* Disable L0s */
	if (pcie->disabled_aspm_states & 0x2) {
		disable_aspm_l10(pcie); /* Disable L1 */
		disable_aspm_l11(pcie); /* Disable L1.1 */
		disable_aspm_l12(pcie); /* Disable L1.2 */
	}
	if (pcie->disabled_aspm_states & 0x4)
		disable_aspm_l11(pcie); /* Disable L1.1 */
	if (pcie->disabled_aspm_states & 0x8)
		disable_aspm_l12(pcie); /* Disable L1.2 */

	val = readl(pci->dbi_base + GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
	writel(val, pci->dbi_base + GEN3_RELATED_OFF);

	if (pcie->update_fc_fixup) {
		dw_pcie_read(pci->dbi_base +
				 CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF, 4, &tmp);
		tmp |= 0x1 << CFG_TIMER_CTRL_ACK_NAK_SHIFT;
		dw_pcie_write(pci->dbi_base +
				  CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF, 4, tmp);
	}

	/* CDM check enable */
	if (pcie->cdm_check) {
		dw_pcie_read(pci->dbi_base +
				 PORT_LOGIC_PL_CHK_REG_CONTROL_STATUS, 4, &tmp);
		tmp |= PORT_LOGIC_PL_CHK_REG_CHK_REG_CONTINUOUS;
		tmp |= PORT_LOGIC_PL_CHK_REG_CHK_REG_START;
		dw_pcie_write(pci->dbi_base +
				  PORT_LOGIC_PL_CHK_REG_CONTROL_STATUS, 4, tmp);
	}

	if (pcie->is_safety_platform) {
		/* Disable HW autonomous speed change */
		val = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL_2);
		val &= ~CFG_LINK_STATUS_CONTROL_2_HW_AUTO_SPEED_DISABLE;
		writel(val, pci->dbi_base + CFG_LINK_STATUS_CONTROL_2);

		/* Disable all ASPM states */
		disable_aspm_l0s(pcie); /* Disable L0s */
		disable_aspm_l10(pcie); /* Disable L1 */
		disable_aspm_l11(pcie); /* Disable L1.1 */
		disable_aspm_l12(pcie); /* Disable L1.2 */
	}

#ifdef CONFIG_PCIE_TEGRA_DW_LANE_MARGIN
	val = readl(pci->dbi_base + GEN4_LANE_MARGINING_1);
	val &= ~GEN4_LANE_MARGINING_1_NUM_TIMING_STEPS_MASK;
	val |= NUM_TIMING_STEPS;
	val &= ~GEN4_LANE_MARGINING_1_MAX_VOLTAGE_OFFSET_MASK;
	val |= (NUM_VOLTAGE_STEPS <<
		GEN4_LANE_MARGINING_1_MAX_VOLTAGE_OFFSET_SHIFT);
	writel(val, pci->dbi_base + GEN4_LANE_MARGINING_1);

	/* Need DBI_RO_WR_EN set to program this bit */
	val = readl(pci->dbi_base + GEN4_LANE_MARGINING_2);
	val |= GEN4_LANE_MARGINING_2_VOLTAGE_SUPPORTED;
#ifdef CONFIG_PCIE_TEGRA_DW_TWO_SIDE_LANE_MARGIN
	val |= GEN4_LANE_MARGINING_2_LEFT_RIGHT_TIMING;
	val |= GEN4_LANE_MARGINING_2_UP_DOWN_VOLTAGE;
#endif
	writel(val, pci->dbi_base + GEN4_LANE_MARGINING_2);
#endif

	dw_pcie_setup_rc(pp);

	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000000);

	/* FPGA specific PHY initialization */
	if (tegra_platform_is_fpga()) {
		val = readl(pcie->appl_base + APPL_GTH_PHY);
		val |= APPL_GTH_PHY_RST;
		writel(val, pcie->appl_base + APPL_GTH_PHY);
	}

	clk_set_rate(pcie->core_clk, GEN4_CORE_CLK_FREQ);

	/* assert RST */
	val = readl(pcie->appl_base + APPL_PINMUX);
	val &= ~APPL_PINMUX_PEX_RST;
	writel(val, pcie->appl_base + APPL_PINMUX);

	usleep_range(100, 200);

	/* enable LTSSM */
	val = readl(pcie->appl_base + APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	/* de-assert RST */
	val = readl(pcie->appl_base + APPL_PINMUX);
	val |= APPL_PINMUX_PEX_RST;
	writel(val, pcie->appl_base + APPL_PINMUX);

	msleep(100);

	val = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL);
	while (!(val & CFG_LINK_STATUS_DLL_ACTIVE)) {
		if (!count) {
			val = readl(pcie->appl_base + APPL_DEBUG);
			val &= APPL_DEBUG_LTSSM_STATE_MASK;
			val >>= APPL_DEBUG_LTSSM_STATE_SHIFT;
			tmp = readl(pcie->appl_base + APPL_LINK_STATUS);
			tmp &= APPL_LINK_STATUS_RDLH_LINK_UP;
			if ((val == 0x11) && !tmp) {
				dev_info(pci->dev, "link is down in DLL, "
					 "try again with DLFE disabled");
				/* disable LTSSM */
				val = readl(pcie->appl_base + APPL_CTRL);
				val &= ~APPL_CTRL_LTSSM_EN;
				writel(val, pcie->appl_base + APPL_CTRL);

				reset_control_assert(pcie->core_rst);
				reset_control_deassert(pcie->core_rst);

				dw_pcie_read(pci->dbi_base +
						 pcie->dl_feature_cap,
						 4, &val);
				val &= ~DL_FEATURE_EXCHANGE_EN;
				dw_pcie_write(pci->dbi_base +
						  pcie->dl_feature_cap,
						  4, val);

				tegra_pcie_dw_host_init(&pcie->pci.pp);
				return 0;
			}
			dev_info(pci->dev, "link is down\n");
			return 0;
		}
		dev_dbg(pci->dev, "polling for link up\n");
		usleep_range(1000, 2000);
		val = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL);
		count--;
	}
	dev_info(pci->dev, "link is up\n");

	return tegra_pcie_enable_interrupts(pp);
}

static void tegra_pcie_dw_host_deinit(struct pcie_port *pp)
{
	if (IS_ENABLED(CONFIG_PCI_MSI))
		dw_pcie_msi_deinit(pp);
}

static int tegra_pcie_dw_link_up(struct dw_pcie *pci)
{
	u32 val = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL);

	return !!(val & CFG_LINK_STATUS_DLL_ACTIVE);
}

static int tegra_pcie_dw_start_link(struct dw_pcie *pci)
{
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);

	enable_irq(pcie->pex_rst_irq);

	return 0;
}

static void tegra_pcie_dw_stop_link(struct dw_pcie *pci)
{
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);

	disable_irq(pcie->pex_rst_irq);
}

static void enable_ltr(struct pci_dev *pdev)
{
	u16 val = 0;
	u32 data = 0;

	pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP2, &data);
	if (data & PCI_EXP_DEVCAP2_LTR) {
		pcie_capability_read_word(pdev, PCI_EXP_DEVCTL2, &val);
		val |= PCI_EXP_DEVCTL2_LTR_EN;
		pcie_capability_write_word(pdev, PCI_EXP_DEVCTL2, val);
	}
}

static void tegra_pcie_dw_scan_bus(struct pcie_port *pp)
{
	struct pci_host_bridge *host = pci_find_host_bridge(pp->bus);
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	struct resource_entry *win;
	struct pci_dev *pdev = NULL, *ppdev = NULL;
	u32 speed = 0, data = 0, pos = 0;
	struct pci_bus *child;
	unsigned long freq, width = 0;

	if (!tegra_pcie_dw_link_up(pci))
		return;

	/* Make EMC FLOOR freq request based on link width and speed */
	data = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL);
	width = ((data >> 16) & PCI_EXP_LNKSTA_NLW) >> 4;
	width = find_first_bit(&width, sizeof(width));
	speed = ((data >> 16) & PCI_EXP_LNKSTA_CLS);
	freq = pcie->dvfs_tbl[width][speed - 1];
	dev_dbg(pcie->dev, "EMC Freq requested = %lu\n", freq);

	if (tegra_bwmgr_set_emc(pcie->emc_bw, freq, TEGRA_BWMGR_SET_EMC_FLOOR))
		dev_err(pcie->dev, "can't set emc clock[%lu]\n", freq);

	speed = ((data >> 16) & PCI_EXP_LNKSTA_CLS);
	clk_set_rate(pcie->core_clk, pcie_gen_freq[speed - 1]);

	if (pcie->is_safety_platform)
		if (clk_prepare_enable(pcie->core_clk_m))
			dev_err(pcie->dev,
				"Failed to enable monitored core clock\n");

	resource_list_for_each_entry(win, &host->windows) {
		if (win->res->flags & IORESOURCE_IO) {
			/* program iATU for IO mapping */
			outbound_atu(pp, PCIE_ATU_REGION_INDEX1,
				     PCIE_ATU_TYPE_IO, pp->io_base,
				     win->res->start - win->offset,
				     resource_size(win->res));
		} else if (win->res->flags & IORESOURCE_PREFETCH) {
			/* program iATU for Non-prefetchable MEM mapping */
			outbound_atu(pp, PCIE_ATU_REGION_INDEX3,
				     PCIE_ATU_TYPE_MEM, win->res->start,
				     win->res->start - win->offset,
				     resource_size(win->res));
		} else if (win->res->flags & IORESOURCE_MEM) {
			/* program iATU for Non-prefetchable MEM mapping */
			outbound_atu(pp, PCIE_ATU_REGION_INDEX2,
				     PCIE_ATU_TYPE_MEM, win->res->start,
				     win->res->start - win->offset,
				     resource_size(win->res));
		}
	}

	list_for_each_entry(child, &pp->bus->children, node) {
		/* L1SS programming only for immediate downstream devices */
		if (child->parent == pp->bus) {
			pdev = pci_get_slot(child, PCI_DEVFN(0, 0));
			pci_dev_put(pdev);
			/*
			 * EP can send LTR message even if L1SS is not enabled,
			 * so enable LTR to avoid treating LTR message as
			 * "unsupported request"
			 */
			ppdev = pci_get_slot(pp->bus, PCI_DEVFN(0, 0));
			pci_dev_put(ppdev);
			enable_ltr(ppdev);	/* Enable LTR in parent (RP) */

			if (!pdev)
				break;
			if (pcie->disable_l1_cpm)
				pci_disable_link_state_locked(pdev,
							PCIE_LINK_STATE_CLKPM);
			pos = pci_find_ext_capability(pdev,
						      PCI_EXT_CAP_ID_L1SS);
			if (!pos)
				continue;
			pci_read_config_dword(pdev, pos + PCI_L1SS_CAP, &data);
			if (!((data & PCI_L1SS_CAP_ASPM_L1_2) ||
			      (data & PCI_L1SS_CAP_PCIPM_L1_2)))
				continue;
			enable_ltr(pdev);	/* Enable LTR in child (EP) */
		}
	}
}

static const struct dw_pcie_ops tegra_dw_pcie_ops = {
	.write_dbi = tegra_pcie_write_dbi,
	.link_up = tegra_pcie_dw_link_up,
	.start_link = tegra_pcie_dw_start_link,
	.stop_link = tegra_pcie_dw_stop_link,
};

static struct dw_pcie_host_ops tegra_pcie_dw_host_ops = {
	.rd_own_conf = tegra_pcie_dw_rd_own_conf,
	.wr_own_conf = tegra_pcie_dw_wr_own_conf,
	.rd_other_conf = tegra_pcie_dw_rd_other_conf,
	.wr_other_conf = tegra_pcie_dw_wr_other_conf,
	.host_init = tegra_pcie_dw_host_init,
	.host_deinit = tegra_pcie_dw_host_deinit,
	.scan_bus = tegra_pcie_dw_scan_bus,
};

static void tegra_pcie_disable_phy(struct tegra_pcie_dw *pcie)
{
	int phy_count = pcie->phy_count;

	while (phy_count--) {
		phy_power_off(pcie->phy[phy_count]);
		phy_exit(pcie->phy[phy_count]);
	}
}

static int tegra_pcie_enable_phy(struct tegra_pcie_dw *pcie)
{
	int phy_count = pcie->phy_count;
	int ret;
	int i;

	for (i = 0; i < phy_count; i++) {
		ret = phy_init(pcie->phy[i]);
		if (ret < 0)
			goto err_phy_init;

		ret = phy_power_on(pcie->phy[i]);
		if (ret < 0) {
			phy_exit(pcie->phy[i]);
			goto err_phy_power_on;
		}
	}

	return 0;

	while (i >= 0) {
		phy_power_off(pcie->phy[i]);
err_phy_power_on:
		phy_exit(pcie->phy[i]);
err_phy_init:
		i--;
	}

	return ret;
}

static int tegra_pcie_dw_parse_dt(struct tegra_pcie_dw *pcie)
{
	struct device_node *np = pcie->dev->of_node;
	int ret = 0;

	/* Parse controller specific register offsets */
	ret = of_property_read_u32(np, "nvidia,cfg-link-cap-l1sub",
				   &pcie->cfg_link_cap_l1sub);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read cfg-link-cap-l1sub: %d\n",
			ret);
		return ret;
	}

	ret = of_property_read_u32(np, "nvidia,event-cntr-ctrl",
				   &pcie->event_cntr_ctrl);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read event-cntr-ctrl: %d\n", ret);
		return ret;
	}
	ret = of_property_read_u32(np, "nvidia,event-cntr-data",
				   &pcie->event_cntr_data);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read event-cntr-data: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32_array(np, "nvidia,dvfs-tbl",
					 &pcie->dvfs_tbl[0][0], 16);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read EMC DVFS table: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "nvidia,aux-clk-freq",
				   &pcie->aux_clk_freq);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read Aux_Clk_Freq: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "nvidia,aspm-cmrt", &pcie->aspm_cmrt);
	if (ret < 0)
		dev_info(pcie->dev, "fail to read ASPM cmrt: %d\n", ret);

	ret = of_property_read_u32(np, "nvidia,aspm-pwr-on-t",
				   &pcie->aspm_pwr_on_t);
	if (ret < 0)
		dev_info(pcie->dev, "fail to read ASPM Power On time: %d\n",
			 ret);

	ret = of_property_read_u32(np, "nvidia,aspm-l0s-entrance-latency",
				   &pcie->aspm_l0s_enter_lat);
	if (ret < 0)
		dev_info(pcie->dev,
			 "fail to read ASPM L0s Entrance latency: %d\n", ret);

	ret = of_property_read_u32(np, "num-lanes", &pcie->num_lanes);
	if (ret < 0) {
		dev_err(pcie->dev, "fail to read num-lanes: %d\n", ret);
		pcie->num_lanes = 0;
	}

	ret = of_property_read_u32(np, "nvidia,max-speed", &pcie->max_speed);
	if ((ret < 0) || (pcie->max_speed < 1 || pcie->max_speed > 4)) {
		dev_err(pcie->dev, "invalid max-speed (err=%d), set to Gen-1\n",
			ret);
		pcie->max_speed = 1;
	}

	ret = of_property_read_u32(np, "nvidia,init-speed", &pcie->init_speed);
	if ((ret < 0) || (pcie->init_speed < 1 || pcie->init_speed > 4)) {
		dev_info(pcie->dev, "Setting init speed to max speed\n");
		pcie->init_speed = pcie->max_speed;
	}

	ret = of_property_read_u32_index(np, "nvidia,controller-id", 1,
					 &pcie->cid);
	if (ret) {
		dev_err(pcie->dev, "Controller-ID is missing in DT: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "nvidia,tsa-config",
				   &pcie->tsa_config_addr);

	pcie->phy_count = of_property_count_strings(np, "phy-names");
	if (pcie->phy_count < 0) {
		dev_err(pcie->dev, "unable to find phy entries\n");
		return pcie->phy_count;
	}

	if (of_property_read_bool(np, "nvidia,update_fc_fixup"))
		pcie->update_fc_fixup = true;

	ret = of_property_read_u32(np, "nvidia,disable-aspm-states",
				   &pcie->disabled_aspm_states);
	if (ret < 0) {
		dev_info(pcie->dev,
			 "Disabling advertisement of all ASPM states\n");
		pcie->disabled_aspm_states = 0xF;
	}

	pcie->enable_srns = of_property_read_bool(pcie->dev->of_node,
						  "nvidia,enable-srns");

	if (pcie->mode == DW_PCIE_RC_TYPE) {
		ret = of_property_read_u32(np, "nvidia,preset-init",
					   &pcie->preset_init);
		if (ret < 0) {
			dev_err(pcie->dev, "fail to read Preset Init: %d\n",
				ret);
			return ret;
		}

		ret = of_property_read_u32(np, "nvidia,cap-pl16g-status",
					   &pcie->cap_pl16g_status);
		if (ret < 0) {
			dev_err(pcie->dev,
				"fail to read cap-pl16g-status: %d\n", ret);
			return ret;
		}

		ret = of_property_read_u32(np, "nvidia,cap-pl16g-cap-off",
					   &pcie->cap_pl16g_cap_off);
		if (ret < 0) {
			dev_err(pcie->dev,
				"fail to read cap-pl16g-cap-off: %d\n", ret);
			return ret;
		}

		/* RC mode specific */
		ret = of_property_read_u32(np, "nvidia,dl-feature-cap",
					   &pcie->dl_feature_cap);
		if (ret < 0) {
			dev_err(pcie->dev,
				"fail to read dl_feature_cap: %d\n", ret);
			return ret;
		}

		pcie->pex_wake = of_get_named_gpio(np, "nvidia,pex-wake", 0);

		pcie->power_down_en = of_property_read_bool(pcie->dev->of_node,
			"nvidia,enable-power-down");

		pcie->disable_l1_cpm =
			device_property_read_bool(pcie->dev,
						  "nvidia,disable-l1-cpm");

#ifdef CONFIG_PCIE_TEGRA_DW_DMA_TEST
		pcie->dma_poll = device_property_read_bool(pcie->dev,
							   "nvidia,dma-poll");
#endif

		pcie->disable_clock_request =
			of_property_read_bool(pcie->dev->of_node,
					      "nvidia,disable-clock-request");

		pcie->cdm_check = of_property_read_bool(np, "nvidia,cdm_check");

		pcie->is_safety_platform = of_property_read_bool(
			pcie->dev->of_node, "nvidia,enable-fmon");

		pcie->n_gpios = of_gpio_named_count(np, "nvidia,plat-gpios");
		if (pcie->n_gpios > 0) {
			int count, gpio;
			enum of_gpio_flags flags;
			unsigned long f;

			pcie->gpios = devm_kzalloc(pcie->dev,
						   pcie->n_gpios * sizeof(int),
						   GFP_KERNEL);
			if (!pcie->gpios)
				return -ENOMEM;

			for (count = 0; count < pcie->n_gpios; ++count) {
				gpio = of_get_named_gpio_flags(np,
							       "nvidia,plat-gpios",
							       count, &flags);
				if ((gpio < 0) && (gpio != -ENOENT))
					return gpio;

				f = (flags & OF_GPIO_ACTIVE_LOW) ?
				    (GPIOF_OUT_INIT_LOW | GPIOF_ACTIVE_LOW) :
				     GPIOF_OUT_INIT_HIGH;

				ret = devm_gpio_request_one(pcie->dev, gpio, f,
							    NULL);
				if (ret < 0) {
					dev_err(pcie->dev,
						"gpio %d request failed\n",
						gpio);
					return ret;
				}
				pcie->gpios[count] = gpio;
			}
		}
	}

	if (pcie->mode == DW_PCIE_EP_TYPE) {
		ret = of_property_read_u16(np, "nvidia,device-id",
					   &pcie->device_id);
		if (ret) {
			dev_err(pcie->dev, "Device-ID is missing in DT: %d\n",
				ret);
			return ret;
		}

		pcie->pex_rst_gpio = of_get_named_gpio(np,
						       "nvidia,pex-rst-gpio",
						       0);
		if (!gpio_is_valid(pcie->pex_rst_gpio)) {
			dev_err(pcie->dev, "pex-rst-gpio is missing\n");
			return pcie->pex_rst_gpio;
		}

		pcie->ep_mode_slot_supplies_en =
			of_property_read_bool(pcie->dev->of_node,
					      "nvidia,enable-slot-supplies");
	}

	return 0;
}

static int tegra_pcie_config_rp(struct tegra_pcie_dw *pcie)
{
	struct pcie_port *pp = &pcie->pci.pp;
	struct dw_pcie *pci = &pcie->pci;
	char *name;
	int ret;

	pp->va_cfg0_base = pci->dbi_base;
	pp->va_cfg1_base = pci->dbi_base + resource_size(pcie->dbi_res) / 2;

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		pp->msi_irq = of_irq_get_byname(pcie->dev->of_node, "msi");
		if (!pp->msi_irq) {
			dev_err(pcie->dev, "failed to get msi interrupt\n");
			return -ENODEV;
		}

		ret = devm_request_irq(pcie->dev, pp->msi_irq,
				       tegra_pcie_msi_irq_handler,
				       IRQF_SHARED | IRQF_NO_THREAD,
				       "tegra-pcie-msi", pp);
		if (ret) {
			dev_err(pcie->dev, "failed to request \"msi\" irq\n");
			return ret;
		}
	}

	pm_runtime_enable(pcie->dev);
	ret = pm_runtime_get_sync(pcie->dev);
	if (ret < 0) {
		dev_err(pcie->dev, "failed to enable pcie dev");
		pm_runtime_disable(pcie->dev);
		return ret;
	}

	pcie->link_state = tegra_pcie_dw_link_up(&pcie->pci);

	if (!pcie->link_state && pcie->power_down_en) {
		ret = -ENOMEDIUM;
		goto fail_host_init;
	}

	name = kasprintf(GFP_KERNEL, "pcie-%u", pcie->cid);
	if (!name) {
		ret = -ENOMEM;
		goto fail_host_init;
	}

	pcie->debugfs = debugfs_create_dir(name, NULL);
	if (!pcie->debugfs)
		dev_err(pcie->dev, "debugfs creation failed\n");
	else
		init_debugfs(pcie);
	kfree(name);

	return ret;

fail_host_init:
	pm_runtime_put_sync(pcie->dev);
	return ret;
}

static int tegra_pcie_init_phy(struct tegra_pcie_dw *pcie)
{
	int phy_count = pcie->phy_count;
	int ret;
	int i;

	for (i = 0; i < phy_count; i++) {
		ret = phy_init(pcie->phy[i]);
		if (ret < 0)
			goto err_phy_init;
	}

	return 0;

	while (i >= 0) {
		phy_exit(pcie->phy[i]);
err_phy_init:
		i--;
	}

	return ret;
}

static int uphy_bpmp_pcie_ep_controller_pll_init(u32 id)
{
	struct mrq_uphy_request req;
	struct mrq_uphy_response resp;

	req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_INIT;
	req.ep_ctrlr_pll_init.ep_controller = id;

	return bpmp_send_uphy_message(&req, sizeof(req), &resp, sizeof(resp));
}

static int uphy_bpmp_pcie_ep_controller_pll_off(u32 id)
{
	struct mrq_uphy_request req;
	struct mrq_uphy_response resp;

	req.cmd = CMD_UPHY_PCIE_EP_CONTROLLER_PLL_OFF;
	req.ep_ctrlr_pll_off.ep_controller = id;

	return bpmp_send_uphy_message(&req, sizeof(req), &resp, sizeof(resp));
}

static int tegra_pcie_power_on_phy(struct tegra_pcie_dw *pcie)
{
	int phy_count = pcie->phy_count;
	int ret;
	int i;

	for (i = 0; i < phy_count; i++) {
		ret = phy_power_on(pcie->phy[i]);
		if (ret < 0)
			goto err_phy_power_on;
	}

	return 0;

	while (i >= 0) {
		phy_power_off(pcie->phy[i]);
err_phy_power_on:
		i--;
	}

	return ret;
}

static void pex_ep_event_pex_rst_assert(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;
	int ret = 0, count = 0;

	if (pcie->ep_state == EP_STATE_DISABLED)
		return;

	/* disable LTSSM */
	val = readl(pcie->appl_base + APPL_CTRL);
	val &= ~APPL_CTRL_LTSSM_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	ret = readl_poll_timeout(pcie->appl_base + APPL_DEBUG, val,
				 ((val & APPL_DEBUG_LTSSM_STATE_MASK) >>
				 APPL_DEBUG_LTSSM_STATE_SHIFT) ==
				 LTSSM_STATE_PRE_DETECT,
				 1, LTSSM_TIMEOUT);
	if (ret)
		dev_info(pcie->dev, "Link didn't go to detect state\n");

	reset_control_assert(pcie->core_rst);

	for (count = 0; count < pcie->phy_count; count++)
		phy_power_off(pcie->phy[count]);

	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);

	/*
	 * If PCIe partition is ungated it will request PLL power ON,
	 * so PLL sequencer will be in SEQ_ON state. To turn off the
	 * PLL sequencer, power gate PCIe partition.
	 */
	ret = pm_runtime_put_sync(pcie->dev);
	if (ret < 0)
		dev_err(pcie->dev, "runtime suspend failed: %d\n", ret);

	if (!(pcie->cid == CTRL_4 && pcie->num_lanes == 1) &&
	    !pcie->enable_srns) {
		/* Resets PLL CAL_VALID and RCAL_VALID */
		ret = uphy_bpmp_pcie_ep_controller_pll_off(pcie->cid);
		if (ret)
			dev_err(pcie->dev, "UPHY off failed for PCIe EP:%d\n",
				ret);
	}

	pcie->ep_state = EP_STATE_DISABLED;
	dev_info(pcie->dev, "EP deinit done\n");
}

static void pex_ep_event_pex_rst_deassert(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_ep *ep = &pci->ep;
	u32 val = 0;
	int ret = 0;

	if (pcie->ep_state == EP_STATE_ENABLED)
		return;

	ret = pm_runtime_get_sync(pcie->dev);
	if (ret < 0) {
		dev_err(pcie->dev, "runtime resume failed: %d\n", ret);
		return;
	}

	if (!(pcie->cid == CTRL_4 && pcie->num_lanes == 1) &&
	    !pcie->enable_srns) {
		ret = uphy_bpmp_pcie_ep_controller_pll_init(pcie->cid);
		if (ret) {
			dev_err(pcie->dev, "UPHY init failed for PCIe EP:%d\n",
				ret);
			goto pll_fail;
		}
	}

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(pcie->dev, "Failed to enable core clock\n");
		goto pll_fail;
	}

	reset_control_deassert(pcie->core_apb_rst);

	ret = tegra_pcie_power_on_phy(pcie);
	if (ret) {
		dev_err(pcie->dev, "failed to power_on phy\n");
		goto phy_fail;
	}

	/* clear any stale interrupt statuses */
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_1);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_2);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_3);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_6);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_7);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_8_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_9);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_10);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_11);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_13);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_14);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_15);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_17);

	/* configure this core for EP mode operation */
	val = readl(pcie->appl_base + APPL_DM_TYPE);
	val &= ~APPL_DM_TYPE_MASK;
	val |= APPL_DM_TYPE_EP;
	writel(val, pcie->appl_base + APPL_DM_TYPE);

	writel(0x0, pcie->appl_base + APPL_CFG_SLCG_OVERRIDE);

	val = readl(pcie->appl_base + APPL_CTRL);
	val |= APPL_CTRL_SYS_PRE_DET_STATE;
	val |= APPL_CTRL_HW_HOT_RST_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	val = readl(pcie->appl_base + APPL_CFG_MISC);
	val |= APPL_CFG_MISC_SLV_EP_MODE;
	val |= (APPL_CFG_MISC_ARCACHE_VAL << APPL_CFG_MISC_ARCACHE_SHIFT);
	writel(val, pcie->appl_base + APPL_CFG_MISC);

	val = readl(pcie->appl_base + APPL_PINMUX);
	val |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN;
	val |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE;
	writel(val, pcie->appl_base + APPL_PINMUX);

	if (tegra_platform_is_fpga()) {
		val = readl(pcie->appl_base + APPL_PINMUX);
		val &= ~APPL_PINMUX_PEX_RST_IN_OVERRIDE_EN;
		writel(val, pcie->appl_base + APPL_PINMUX);
	}

	/* update CFG base address */
	writel(pcie->dbi_res->start & APPL_CFG_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_BASE_ADDR);

	/* update iATU_DMA base address */
	writel(pcie->atu_dma_res->start &
	       APPL_CFG_IATU_DMA_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_IATU_DMA_BASE_ADDR);

	/* enable PEX_RST interrupt generation */
	val = readl(pcie->appl_base + APPL_INTR_EN_L0_0);
	val |= APPL_INTR_EN_L0_0_SYS_INTR_EN;
	if (tegra_platform_is_fpga())
		val |= APPL_INTR_EN_L0_0_PEX_RST_INT_EN;
	val |= APPL_INTR_EN_L0_0_LINK_STATE_INT_EN;
	val |= APPL_INTR_EN_L0_0_PCI_CMD_EN_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L0_0);

	val = readl(pcie->appl_base + APPL_INTR_EN_L1_0_0);
	val |= APPL_INTR_EN_L1_0_0_HOT_RESET_DONE_INT_EN;
	val |= APPL_INTR_EN_L1_0_0_RDLH_LINK_UP_INT_EN;
	writel(val, pcie->appl_base + APPL_INTR_EN_L1_0_0);

	reset_control_deassert(pcie->core_rst);

	/* FPGA specific PHY initialization */
	if (tegra_platform_is_fpga()) {
		val = readl(pcie->appl_base + APPL_GTH_PHY);
		val &= ~APPL_GTH_PHY_RST;
		writel(val, pcie->appl_base + APPL_GTH_PHY);
		usleep_range(900, 1100);

		val = readl(pcie->appl_base + APPL_GTH_PHY);
		val &= 0xFFFF0000;
		val |= 0x780; /* required for multiple L1.2 entries */
		val |= APPL_GTH_PHY_RST;
		writel(val, pcie->appl_base + APPL_GTH_PHY);
		usleep_range(900, 1100);
	}

	val = readl(pci->dbi_base + AUX_CLK_FREQ);
	val &= ~(0x3FF);
	if (tegra_platform_is_fpga())
		val |= 0x6;
	else
		val |= 19;	/* CHECK: for Silicon */
	writel(val, pci->dbi_base + AUX_CLK_FREQ);

	if (pcie->update_fc_fixup) {
		val = readl(pci->dbi_base +
			    CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF);
		val |= 0x1 << CFG_TIMER_CTRL_ACK_NAK_SHIFT;
		writel(val, pci->dbi_base +
		       CFG_TIMER_CTRL_MAX_FUNC_NUM_OFF);
	}

	program_gen3_gen4_eq_presets(pcie);

	val = readl(pci->dbi_base + PORT_LOGIC_MISC_CONTROL);
	val |= PORT_LOGIC_MISC_CONTROL_DBI_RO_WR_EN;
	writel(val, pci->dbi_base + PORT_LOGIC_MISC_CONTROL);

	/* Program T_cmrt and T_pwr_on values */
	val = readl(pci->dbi_base + pcie->cfg_link_cap_l1sub);
	val &= ~(PCI_L1SS_CAP_CM_RTM_MASK | PCI_L1SS_CAP_PWRN_VAL_MASK);
	val |= (0x3C << PCI_L1SS_CAP_CM_RTM_SHIFT);	/* 60us */
	val |= (0x14 << PCI_L1SS_CAP_PWRN_VAL_SHIFT);	/* 40us */
	writel(val, pci->dbi_base + pcie->cfg_link_cap_l1sub);

	/* Program L0s and L1 entrance latencies */
	val = readl(pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);
	val &= ~(L0S_ENTRANCE_LAT_MASK | L1_ENTRANCE_LAT_MASK);
	val |= (0x3 << L0S_ENTRANCE_LAT_SHIFT);	/* 4us */
	val |= (0x5 << L1_ENTRANCE_LAT_SHIFT);	/* 32us */
	val |= ENTER_ASPM;
	writel(val, pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);

	if (pcie->disabled_aspm_states & 0x1)
		disable_aspm_l0s(pcie); /* Disable L0s */
	if (pcie->disabled_aspm_states & 0x2) {
		disable_aspm_l10(pcie); /* Disable L1 */
		disable_aspm_l11(pcie); /* Disable L1.1 */
		disable_aspm_l12(pcie); /* Disable L1.2 */
	}
	if (pcie->disabled_aspm_states & 0x4)
		disable_aspm_l11(pcie); /* Disable L1.1 */
	if (pcie->disabled_aspm_states & 0x8)
		disable_aspm_l12(pcie); /* Disable L1.2 */

	/* Enable ASPM counters */
	val = EVENT_COUNTER_ENABLE_ALL << EVENT_COUNTER_ENABLE_SHIFT;
	val |= EVENT_COUNTER_GROUP_5 << EVENT_COUNTER_GROUP_SEL_SHIFT;
	writel(val, pci->dbi_base + pcie->event_cntr_ctrl);

	val = readl(pci->dbi_base + GEN3_RELATED_OFF);
	val &= ~GEN3_RELATED_OFF_GEN3_ZRXDC_NONCOMPL;
	writel(val, pci->dbi_base + GEN3_RELATED_OFF);

	writew(pcie->device_id, pci->dbi_base + PCI_DEVICE_ID);

	/* Configure N_FTS & FTS */
	val = readl(pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);
	val &= ~(N_FTS_MASK << N_FTS_SHIFT);
	val |= N_FTS_VAL << N_FTS_SHIFT;
	writel(val, pci->dbi_base + PORT_LOGIC_ACK_F_ASPM_CTRL);

	val = readl(pci->dbi_base + PORT_LOGIC_GEN2_CTRL);
	val &= ~FTS_MASK;
	val |= FTS_VAL;
	writel(val, pci->dbi_base + PORT_LOGIC_GEN2_CTRL);

	if (pcie->max_speed >= 1 && pcie->max_speed <= 4) {
		val = readl(pci->dbi_base + EP_CFG_LINK_CAP);
		val &= ~EP_CFG_LINK_CAP_MAX_SPEED_MASK;
		val |= pcie->max_speed;
		writel(val, pci->dbi_base + EP_CFG_LINK_CAP);
	}

	writew(PCI_CLASS_MEMORY_OTHER,
	       pci->dbi_base + PCI_CLASS_DEVICE);

	val = readl(pci->dbi_base + PORT_LOGIC_MISC_CONTROL);
	val &= ~PORT_LOGIC_MISC_CONTROL_DBI_RO_WR_EN;
	writel(val, pci->dbi_base + PORT_LOGIC_MISC_CONTROL);

	clk_set_rate(pcie->core_clk, GEN4_CORE_CLK_FREQ);

	/* Automatic triggering of MSI-X generation when PCIe MWr is issued
	 * with an address equal to MSIX_ADDRESS_MATCH_LOW_OFF and
	 * MSIX_ADDRESS_MATCH_HIGH_OFF. Program these registers with
	 * base address allocated in dw_pcie_ep_init().
	 */

	val = (ep->msi_mem_phys & MSIX_ADDR_MATCH_LOW_OFF_MASK);
	val |= MSIX_ADDR_MATCH_LOW_OFF_EN;
	writel(val, pci->dbi_base + MSIX_ADDR_MATCH_LOW_OFF);
	val = ((ep->msi_mem_phys >> 32) & MSIX_ADDR_MATCH_HIGH_OFF_MASK);
	writel(val, pci->dbi_base + MSIX_ADDR_MATCH_HIGH_OFF);

	dw_pcie_set_regs_available(pci);

	/* enable LTSSM */
	val = readl(pcie->appl_base + APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	pcie->ep_state = EP_STATE_ENABLED;
	dev_info(pcie->dev, "EP init done\n");

	return;

phy_fail:
	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);
pll_fail:
	ret = pm_runtime_put_sync(pcie->dev);
	if (ret < 0)
		dev_err(pcie->dev, "runtime suspend failed: %d\n", ret);

}

static void pex_ep_event_hot_rst_done(struct tegra_pcie_dw *pcie)
{
	u32 val = 0;

	/* SW FixUp required during hot reset */
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_0_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_1);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_2);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_3);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_6);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_7);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_8_0);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_9);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_10);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_11);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_13);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_14);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_15);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_INTR_STATUS_L1_17);
	writel(0xFFFFFFFF, pcie->appl_base + APPL_MSI_CTRL_2);

	val = readl(pcie->appl_base + APPL_CTRL);
	val |= APPL_CTRL_LTSSM_EN;
	writel(val, pcie->appl_base + APPL_CTRL);
}

static void pex_ep_event_bme_change(struct tegra_pcie_dw *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	u32 val = 0, speed = 0;
	unsigned long freq, width = 0;

	/* Make EMC FLOOR freq request based on link width and speed */
	val = readl(pci->dbi_base + CFG_LINK_STATUS_CONTROL);
	width = ((val >> 16) & PCI_EXP_LNKSTA_NLW) >> 4;
	width = find_first_bit(&width, sizeof(width));
	speed = ((val >> 16) & PCI_EXP_LNKSTA_CLS);
	freq = pcie->dvfs_tbl[width][speed - 1];
	dev_dbg(pcie->dev, "EMC Freq requested = %lu\n", freq);

	if (tegra_bwmgr_set_emc(pcie->emc_bw, freq, TEGRA_BWMGR_SET_EMC_FLOOR))
		dev_err(pcie->dev, "can't set emc clock[%lu]\n", freq);

	speed = ((val >> 16) & PCI_EXP_LNKSTA_CLS);
	clk_set_rate(pcie->core_clk, pcie_gen_freq[speed - 1]);

	/* If EP doesn't advertise L1SS, just return */
	val = readl(pci->dbi_base + pcie->cfg_link_cap_l1sub);
	if (!(val & (PCI_L1SS_CAP_ASPM_L1_1 | PCI_L1SS_CAP_ASPM_L1_2)))
		return;

	/* Check if BME is set to '1' */
	val = readl(pci->dbi_base + EP_CS_STATUS_COMMAND);
	if (val & EP_CS_STATUS_COMMAND_BME) {
		ktime_t timeout;

		/* 110us for both snoop and no-snoop */
		val = 110 | (2 << PCI_LTR_SCALE_SHIFT) | LTR_MSG_REQ;
		val |= (val << LTR_MST_NO_SNOOP_SHIFT);
		writel(val, pcie->appl_base + APPL_LTR_MSG_1);
		/* Send LTR upstream */
		val = readl(pcie->appl_base + APPL_LTR_MSG_2);
		val |= APPL_LTR_MSG_2_LTR_MSG_REQ_STATE;
		writel(val, pcie->appl_base + APPL_LTR_MSG_2);

		timeout = ktime_add_us(ktime_get(), LTR_MSG_TIMEOUT);
		for (;;) {
			val = readl(pcie->appl_base + APPL_LTR_MSG_2);
			if (!(val & APPL_LTR_MSG_2_LTR_MSG_REQ_STATE))
				break;
			if (ktime_after(ktime_get(), timeout))
				break;
			usleep_range(1000, 1100);
		}
		if (val & APPL_LTR_MSG_2_LTR_MSG_REQ_STATE)
			dev_err(pcie->dev, "LTR_MSG sending failed\n");
	}
}

static int pcie_ep_work_thread(void *p)
{
	struct tegra_pcie_dw *pcie = (struct tegra_pcie_dw *)p;
	u32 event = 0;

	while (1) {
		wait_event_interruptible(pcie->wq,
					 !kfifo_is_empty(&pcie->event_fifo));

		if (kthread_should_stop())
			break;

		if (!kfifo_get(&pcie->event_fifo, &event)) {
			dev_warn(pcie->dev, "empty kfifo\n");
			continue;
		}

		switch (event) {
		case EP_PEX_RST_DEASSERT:
			dev_dbg(pcie->dev, "EP_EVENT: EP_PEX_RST_DEASSERT\n");
			pex_ep_event_pex_rst_deassert(pcie);
			break;

		case EP_PEX_RST_ASSERT:
			dev_dbg(pcie->dev, "EP_EVENT: EP_PEX_RST_ASSERT\n");
			pex_ep_event_pex_rst_assert(pcie);
			break;

		case EP_PEX_HOT_RST_DONE:
			dev_dbg(pcie->dev, "EP_EVENT: EP_PEX_HOT_RST_DONE\n");
			pex_ep_event_hot_rst_done(pcie);
			break;

		case EP_PEX_BME_CHANGE:
			dev_dbg(pcie->dev, "EP_EVENT: EP_PEX_BME_CHANGE\n");
			pex_ep_event_bme_change(pcie);
			break;

		default:
			dev_warn(pcie->dev, "Invalid PCIe EP event\n");
			break;
		}
	}
	return 0;
}

static irqreturn_t pex_rst_isr(int irq, void *arg)
{
	struct tegra_pcie_dw *pcie = arg;

	if (gpio_get_value(pcie->pex_rst_gpio)) {
		dev_dbg(pcie->dev, "EVENT: EP_PEX_RST_DEASSERT\n");
		if (!kfifo_put(&pcie->event_fifo, EP_PEX_RST_DEASSERT)) {
			dev_err(pcie->dev, "EVENT: fifo is full\n");
			return IRQ_HANDLED;
		}
	} else {
		dev_dbg(pcie->dev, "EVENT: EP_PEX_RST_ASSERT\n");
		if (!kfifo_put(&pcie->event_fifo, EP_PEX_RST_ASSERT)) {
			dev_err(pcie->dev, "EVENT: fifo is full\n");
			return IRQ_HANDLED;
		}
	}

	wake_up(&pcie->wq);
	return IRQ_HANDLED;
}

static void tegra_pcie_ep_setup(struct dw_pcie_ep *ep)
{
}

static void tegra_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);
	struct gpio_desc *gpiod;
	int ret = 0;
	int irq;

	pcie->ep_state = EP_STATE_DISABLED;

	ret = tegra_pcie_init_phy(pcie);
	if (ret) {
		dev_err(pcie->dev, "failed to init phy\n");
		goto fail_pinctrl;
	}

	INIT_KFIFO(pcie->event_fifo);

	init_waitqueue_head(&pcie->wq);

	pcie->pcie_ep_task = kthread_run(pcie_ep_work_thread, (void *)pcie,
					 "pcie_ep_work");
	if (IS_ERR(pcie->pcie_ep_task)) {
		dev_err(pcie->dev, "failed to create pcie_ep_work thread\n");
		ret = PTR_ERR(pcie->pcie_ep_task);
		goto fail_bwmgr;
	}

	ret = devm_gpio_request(pcie->dev, pcie->pex_rst_gpio, "pex_rst_gpio");
	if (ret < 0) {
		dev_err(pcie->dev, "pex_rst_gpio request failed\n");
		goto fail_thread;
	}
	ret = gpio_direction_input(pcie->pex_rst_gpio);
	if (ret < 0) {
		dev_err(pcie->dev, "pex_rst_gpio direction input failed\n");
		goto fail_thread;
	}

	if (pcie->cid == CTRL_5) {
		gpiod = gpio_to_desc(pcie->pex_rst_gpio);
		if (!gpiod) {
			dev_err(pcie->dev, "Unable to get gpio desc\n");
			ret = -EINVAL;
			goto fail_thread;
		}
		ret = gpiod_set_debounce(gpiod, PERST_DEBOUNCE_TIME);
		if (ret < 0) {
			dev_err(pcie->dev, "Unable to set gpio debounce time\n");
			goto fail_thread;
		}
	}

	irq = gpio_to_irq(pcie->pex_rst_gpio);
	if (irq < 0) {
		dev_err(pcie->dev, "Unable to get irq for pex_rst_gpio\n");
		ret = irq;
		goto fail_thread;
	}

	pcie->pex_rst_irq = irq;

	ret = devm_request_irq(pcie->dev, (unsigned int)irq, pex_rst_isr,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       "pex_rst", (void *)pcie);
	if (ret < 0) {
		dev_err(pcie->dev, "Unable to request irq for pex_rst\n");
		goto fail_thread;
	}
	disable_irq(irq);
	pm_runtime_enable(pcie->dev);

	return;

fail_thread:
	kthread_stop(pcie->pcie_ep_task);
fail_bwmgr:
	tegra_pcie_disable_phy(pcie);
fail_pinctrl:
	return;
}

static void get_slot_regulators(struct tegra_pcie_dw *pcie)
{
	if (pcie->cid != CTRL_5)
		return;

	pcie->slot_ctl_3v3 = devm_regulator_get_optional(pcie->dev, "vpcie3v3");
	if (IS_ERR(pcie->slot_ctl_3v3))
		dev_info(pcie->dev, "Failed to get 3V slot regulator: %ld\n",
			 PTR_ERR(pcie->slot_ctl_3v3));

	pcie->slot_ctl_12v = devm_regulator_get_optional(pcie->dev, "vpcie12v");
	if (IS_ERR(pcie->slot_ctl_12v))
		dev_info(pcie->dev, "Failed to get 12V slot regulator: %ld\n",
			 PTR_ERR(pcie->slot_ctl_12v));
}

static int enable_slot_regulators(struct tegra_pcie_dw *pcie)
{
	int ret;

	if (pcie->cid != CTRL_5)
		return 0;

	if (!IS_ERR(pcie->slot_ctl_3v3)) {
		ret = regulator_enable(pcie->slot_ctl_3v3);
		if (ret < 0) {
			dev_err(pcie->dev,
				"Enabling 3V3 supply to slot failed: %d\n",
				ret);
			return ret;
		}
	}

	if (!IS_ERR(pcie->slot_ctl_12v)) {
		ret = regulator_enable(pcie->slot_ctl_12v);
		if (ret < 0) {
			dev_err(pcie->dev,
				"Enabling 12V supply to slot failed: %d\n",
				ret);
			regulator_disable(pcie->slot_ctl_3v3);
			return ret;
		}
	}

	/*
	 * According to PCI Express Card Electromechanical Specification
	 * Revision 1.1, Table-2.4, T_PVPERL (Power stable to PERST# inactive)
	 * should be a minimum of 100ms.
	 */
	if (pcie->slot_ctl_3v3 || pcie->slot_ctl_12v)
		msleep(100);

	return 0;
}

static void disable_slot_regulators(struct tegra_pcie_dw *pcie)
{
	if (pcie->cid != CTRL_5)
		return;

	if (!IS_ERR(pcie->slot_ctl_3v3))
		regulator_disable(pcie->slot_ctl_12v);
	if (!IS_ERR(pcie->slot_ctl_12v))
		regulator_disable(pcie->slot_ctl_3v3);
}

static int tegra_pcie_raise_legacy_irq(struct tegra_pcie_dw *pcie)
{
	/* There is no support from HW to raise a legacy irq apart from
	 * getting it generated by DMA engine upon completing some task
	 */
	 return -EPERM;
}

static int tegra_pcie_raise_msi_irq(struct tegra_pcie_dw *pcie,
				     u8 irq)
{
	if (unlikely(irq > 31))
		return -EINVAL;

	writel((1 << irq), pcie->appl_base + APPL_MSI_CTRL_1);
	return 0;
}

static int tegra_pcie_raise_msix_irq(struct tegra_pcie_dw *pcie,
				     u8 irq)
{
	struct dw_pcie_ep *ep = &pcie->pci.ep;

	writel(irq, ep->msi_mem);
	return 0;
}

static int tegra_pcie_raise_irq(struct dw_pcie_ep *ep,
				enum pci_epc_irq_type type, u8 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	struct tegra_pcie_dw *pcie = dw_pcie_to_tegra_pcie(pci);

	switch (type) {
	case PCI_EPC_IRQ_LEGACY:
		return tegra_pcie_raise_legacy_irq(pcie);
		break;
	case PCI_EPC_IRQ_MSI:
		return tegra_pcie_raise_msi_irq(pcie, interrupt_num);
		break;
	case PCI_EPC_IRQ_MSIX:
		return tegra_pcie_raise_msix_irq(pcie, interrupt_num);
		break;
	default:
		dev_err(pci->dev, "UNKNOWN IRQ type\n");
	}

	return 0;
}

static struct dw_pcie_ep_ops pcie_ep_ops = {
	.ep_init = tegra_pcie_ep_init,
	.ep_setup = tegra_pcie_ep_setup,
	.raise_irq = tegra_pcie_raise_irq,
};

static int tegra_pcie_config_ep(struct tegra_pcie_dw *pcie,
				struct platform_device *pdev)
{
	int ret;
	struct dw_pcie_ep *ep;
	struct resource *res;
	struct device *dev = pcie->dev;
	struct dw_pcie *pci = &pcie->pci;

	ep = &pci->ep;
	ep->ops = &pcie_ep_ops;

	ep->hw_regs_not_available = true;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "window2");
	if (!res) {
		dev_err(dev, "failed to get window2\n");
		return -EINVAL;
	}

	ep->phys_base = res->start;
	ep->addr_size = resource_size(res);
	ep->page_size = SZ_64K;

	if (pcie->ep_mode_slot_supplies_en) {
		ret = enable_slot_regulators(pcie);
		if (ret)
			return ret;
	}

	ret = dw_pcie_ep_init(ep);
	if (ret) {
		dev_err(dev, "failed to initialize endpoint\n");
		goto fail_disable_regulators;
	}

	return 0;

fail_disable_regulators:
	if (pcie->ep_mode_slot_supplies_en)
		disable_slot_regulators(pcie);

	return ret;
}

static const struct tegra_pcie_of_data tegra_pcie_rc_of_data = {
	.mode = DW_PCIE_RC_TYPE,
};

static const struct tegra_pcie_of_data tegra_pcie_ep_of_data = {
	.mode = DW_PCIE_EP_TYPE,
};

static const struct of_device_id tegra_pcie_dw_of_match[] = {
	{
		.compatible = "nvidia,tegra194-pcie",
		.data = &tegra_pcie_rc_of_data,
	},
	{
		.compatible = "nvidia,tegra194-pcie-ep",
		.data = &tegra_pcie_ep_of_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, tegra_pcie_dw_of_match);

static int tegra_pcie_dw_probe(struct platform_device *pdev)
{
	struct tegra_pcie_dw *pcie;
	struct pcie_port *pp;
	struct dw_pcie *pci;
	struct phy **phy;
	struct resource *appl_res;
	struct pinctrl *pin = NULL;
	struct pinctrl_state *pin_state = NULL;
	char *name;
	int ret, i = 0;
	u32 val = 0;
	const struct of_device_id *match;
	const struct tegra_pcie_of_data *data;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = &pdev->dev;
	pci->ops = &tegra_dw_pcie_ops;
	pp = &pci->pp;
	pcie->dev = &pdev->dev;

	match = of_match_device(of_match_ptr(tegra_pcie_dw_of_match),
				&pdev->dev);
	if (!match)
		return -EINVAL;

	data = (struct tegra_pcie_of_data *)match->data;
	pcie->mode = (enum dw_pcie_device_mode)data->mode;

	ret = tegra_pcie_dw_parse_dt(pcie);
	if (ret < 0) {
		dev_err(pcie->dev, "DT parsing failed: %d\n", ret);
		return ret;
	}

	if (pcie->mode == DW_PCIE_RC_TYPE) {
		pcie->td_bit = pcie_is_ecrc_enabled();

		if (gpio_is_valid(pcie->pex_wake)) {
			ret = devm_gpio_request(pcie->dev, pcie->pex_wake,
						"pcie_wake");
			if (ret < 0) {
				if (ret == -EBUSY) {
					dev_err(pcie->dev,
						"pex_wake already in use\n");
					pcie->pex_wake = -EINVAL;
				} else {
					dev_err(pcie->dev,
						"pcie_wake gpio_request failed %d\n",
						ret);
					return ret;
				}
			}
			if (gpio_is_valid(pcie->pex_wake)) {
				ret = gpio_direction_input(pcie->pex_wake);
				if (ret < 0) {
					dev_err(pcie->dev,
						"%s: pcie_wake gpio_direction_input failed %d\n",
						__func__, ret);
					return ret;
				}
				device_init_wakeup(pcie->dev, true);
			}
		}
	}

	if (pcie->tsa_config_addr) {
		void __iomem *tsa_addr;

		tsa_addr = ioremap(pcie->tsa_config_addr, 4);
		val = readl(tsa_addr);
		val |= TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_HUB2 <<
		       TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_SHIFT;
		writel(val, tsa_addr);
		iounmap(tsa_addr);
	}

	pin = devm_pinctrl_get(pcie->dev);
	if (IS_ERR(pin)) {
		ret = PTR_ERR(pin);
		dev_err(pcie->dev, "pinctrl_get failed: %d\n", ret);
		return ret;
	}
	pin_state = pinctrl_lookup_state(pin, "pex_rst");
	if (!IS_ERR(pin_state)) {
		ret = pinctrl_select_state(pin, pin_state);
		if (ret < 0) {
			dev_err(pcie->dev, "setting pex_rst state fail: %d\n",
				ret);
			return ret;
		}
	}
	pin_state = pinctrl_lookup_state(pin, "clkreq");
	if (!IS_ERR(pin_state)) {
		ret = pinctrl_select_state(pin, pin_state);
		if (ret < 0) {
			dev_err(pcie->dev, "setting clkreq state fail: %d\n",
				ret);
			return ret;
		}
	}

	pcie->pex_ctl_reg = devm_regulator_get(&pdev->dev, "vddio-pex-ctl");
	if (IS_ERR(pcie->pex_ctl_reg)) {
		dev_err(&pdev->dev, "fail to get regulator: %ld\n",
			PTR_ERR(pcie->pex_ctl_reg));
		return PTR_ERR(pcie->pex_ctl_reg);
	}

	get_slot_regulators(pcie);

	pcie->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(pcie->core_clk)) {
		dev_err(&pdev->dev, "Failed to get core clock\n");
		return PTR_ERR(pcie->core_clk);
	}

	if (pcie->is_safety_platform) {
		pcie->core_clk_m = devm_clk_get(&pdev->dev, "core_clk_m");
		if (IS_ERR(pcie->core_clk_m)) {
			dev_err(&pdev->dev, "Failed to get monitor clock\n");
			return PTR_ERR(pcie->core_clk_m);
		}
	}

	appl_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "appl");
	if (!appl_res) {
		dev_err(&pdev->dev, "missing appl space\n");
		return PTR_ERR(appl_res);
	}
	pcie->appl_base = devm_ioremap_resource(&pdev->dev, appl_res);
	if (IS_ERR(pcie->appl_base)) {
		dev_err(&pdev->dev, "mapping appl space failed\n");
		return PTR_ERR(pcie->appl_base);
	}

	pcie->core_apb_rst = devm_reset_control_get(pcie->dev, "core_apb_rst");
	if (IS_ERR(pcie->core_apb_rst)) {
		dev_err(pcie->dev, "PCIE : core_apb_rst reset is missing\n");
		return PTR_ERR(pcie->core_apb_rst);
	}

	phy = devm_kcalloc(pcie->dev, pcie->phy_count, sizeof(*phy),
			   GFP_KERNEL);
	if (!phy)
		return PTR_ERR(phy);

	for (i = 0; i < pcie->phy_count; i++) {
		name = kasprintf(GFP_KERNEL, "pcie-p2u-%u", i);
		if (!name) {
			dev_err(pcie->dev, "failed to create p2u string\n");
			return -ENOMEM;
		}
		phy[i] = devm_phy_get(pcie->dev, name);
		kfree(name);
		if (IS_ERR(phy[i])) {
			ret = PTR_ERR(phy[i]);
			dev_err(pcie->dev, "phy_get error: %d\n", ret);
			return ret;
		}
	}

	pcie->phy = phy;

	pcie->dbi_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "config");
	if (!pcie->dbi_res) {
		dev_err(&pdev->dev, "missing config space\n");
		return PTR_ERR(pcie->dbi_res);
	}

	pci->dbi_base = devm_ioremap_resource(&pdev->dev, pcie->dbi_res);
	if (IS_ERR(pci->dbi_base)) {
		dev_err(&pdev->dev, "mapping dbi space failed\n");
		return PTR_ERR(pci->dbi_base);
	}
	/* Tegra HW locates DBI2 at a fixed offset from DBI */
	pci->dbi_base2 = pci->dbi_base + 0x1000;

	pcie->atu_dma_res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							 "atu_dma");
	if (!pcie->atu_dma_res) {
		dev_err(&pdev->dev, "missing atu_dma space\n");
		return PTR_ERR(pcie->atu_dma_res);
	}

	pci->atu_base = devm_ioremap_resource(&pdev->dev, pcie->atu_dma_res);
	if (IS_ERR(pci->atu_base)) {
		dev_err(&pdev->dev, "mapping atu_dma space failed\n");
		return PTR_ERR(pci->atu_base);
	}

	pci->iatu_unroll_enabled = true;

	pcie->core_rst = devm_reset_control_get(pcie->dev, "core_rst");
	if (IS_ERR(pcie->core_rst)) {
		dev_err(pcie->dev, "PCIE : core_rst reset is missing\n");
		return PTR_ERR(pcie->core_rst);
	}

	pp->irq = platform_get_irq_byname(pdev, "intr");
	if (!pp->irq) {
		dev_err(pcie->dev, "failed to get intr interrupt\n");
		return -ENODEV;
	}

	ret = devm_request_irq(&pdev->dev, pp->irq, tegra_pcie_irq_handler,
			       IRQF_SHARED, "tegra-pcie-intr", pcie);
	if (ret) {
		dev_err(pcie->dev, "failed to request \"intr\" irq\n");
		return ret;
	}

	pcie->emc_bw = tegra_bwmgr_register(pcie_emc_client_id[pcie->cid]);
	if (IS_ERR_OR_NULL(pcie->emc_bw)) {
		dev_err(pcie->dev, "bwmgr registration failed\n");
		ret = -ENOENT;
		return ret;
	}

	platform_set_drvdata(pdev, pcie);

	if (pcie->mode == DW_PCIE_RC_TYPE) {
		ret = tegra_pcie_config_rp(pcie);
		if (ret == -ENOMEDIUM)
			ret = 0;
	} else if (pcie->mode == DW_PCIE_EP_TYPE) {
		tegra_pcie_config_ep(pcie, pdev);
	}

	return ret;
}

static int tegra_pcie_try_link_l2(struct tegra_pcie_dw *pcie)
{
	u32 val;

	if (!tegra_pcie_dw_link_up(&pcie->pci))
		return 0;

	val = readl(pcie->appl_base + APPL_RADM_STATUS);
	val |= APPL_PM_XMT_TURNOFF_STATE;
	writel(val, pcie->appl_base + APPL_RADM_STATUS);

	return readl_poll_timeout_atomic(pcie->appl_base + APPL_DEBUG, val,
				 val & APPL_DEBUG_PM_LINKST_IN_L2_LAT,
				 1, PME_ACK_TIMEOUT);
}

static void tegra_pcie_downstream_dev_to_D0(struct tegra_pcie_dw *pcie)
{
	struct pci_dev *pdev = NULL;
	struct pci_bus *child;
	struct pcie_port *pp = &pcie->pci.pp;

	list_for_each_entry(child, &pp->bus->children, node) {
		/* Bring downstream devices to D0 if they are not already in */
		if (child->parent == pp->bus) {
			pdev = pci_get_slot(child, PCI_DEVFN(0, 0));
			pci_dev_put(pdev);
			if (!pdev)
				break;

			if (pci_set_power_state(pdev, PCI_D0))
				dev_err(pcie->dev, "D0 transition failed\n");
		}
	}
}

static int tegra_pcie_dw_pme_turnoff(struct tegra_pcie_dw *pcie)
{
	u32 data;
	int err, ret = 0;

	if (!tegra_pcie_dw_link_up(&pcie->pci)) {
		dev_info(pcie->dev, "PCIe link is not up...!\n");
		return -1;
	}

	/*
	 * PCIe controller exit from L2 only if reset is applied, so
	 * controller doesn't handle interrupts. But in cases where
	 * L2 entry fails, PERST# asserted which can trigger surprise
	 * link down AER. However this function call happens in
	 * suspend_noirq(), so AER interrupt will not be processed.
	 * Disable all interrupts to avoid such scenario.
	 */
	writel(0x0, pcie->appl_base + APPL_INTR_EN_L0_0);

	if (tegra_pcie_try_link_l2(pcie)) {
		ret = -1;
		dev_info(pcie->dev, "Link didn't transit to L2 state\n");
		/*
		 * TX lane clock freq will reset to Gen1 only if link is in L2
		 * or detect state. So apply pex_rst to end point to force RP
		 * to go into detect state.
		 */
		data = readl(pcie->appl_base + APPL_PINMUX);
		data &= ~APPL_PINMUX_PEX_RST;
		writel(data, pcie->appl_base + APPL_PINMUX);

		err = readl_poll_timeout_atomic(pcie->appl_base + APPL_DEBUG,
						data,
						((data &
						APPL_DEBUG_LTSSM_STATE_MASK) >>
						APPL_DEBUG_LTSSM_STATE_SHIFT) ==
						LTSSM_STATE_PRE_DETECT,
						1, LTSSM_TIMEOUT);

		/*
		 * Some cards might not go to detect state after deasserting
		 * PERST#. Deassert LTSSM to bring link to detect state.
		 */
		data = readl(pcie->appl_base + APPL_CTRL);
		data &= ~APPL_CTRL_LTSSM_EN;
		writel(data, pcie->appl_base + APPL_CTRL);

		err = readl_poll_timeout_atomic(pcie->appl_base + APPL_DEBUG,
						data,
						((data &
						APPL_DEBUG_LTSSM_STATE_MASK) >>
						APPL_DEBUG_LTSSM_STATE_SHIFT) ==
						LTSSM_STATE_PRE_DETECT,
						1, LTSSM_TIMEOUT);
		if (err)
			dev_info(pcie->dev, "Link didn't go to detect state\n");
	}
	/* DBI registers may not be accessible after this as PLL-E would be
	 * down depending on how CLKREQ is pulled by end point
	 */
	data = readl(pcie->appl_base + APPL_PINMUX);
	data |= (APPL_PINMUX_CLKREQ_OVERRIDE_EN | APPL_PINMUX_CLKREQ_OVERRIDE);
	/* Cut REFCLK to slot */
	data |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN;
	data &= ~APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE;
	writel(data, pcie->appl_base + APPL_PINMUX);

	return ret;
}

static int tegra_pcie_dw_remove(struct platform_device *pdev)
{
	struct tegra_pcie_dw *pcie = platform_get_drvdata(pdev);

	if (pcie->mode == DW_PCIE_RC_TYPE) {
		if (!pcie->link_state && pcie->power_down_en)
			return 0;

		destroy_dma_test_debugfs(pcie);
		debugfs_remove_recursive(pcie->debugfs);
		pm_runtime_put_sync(pcie->dev);
		pm_runtime_disable(pcie->dev);
	} else if (pcie->mode == DW_PCIE_EP_TYPE) {
		dw_pcie_ep_exit(&pcie->pci.ep);
		if (!kfifo_put(&pcie->event_fifo, EP_EVENT_EXIT))
			dev_err(pcie->dev, "EVENT: fifo is full\n");
		kthread_stop(pcie->pcie_ep_task);

		tegra_pcie_disable_phy(pcie);

		regulator_disable(pcie->pex_ctl_reg);

		if (pcie->cid != CTRL_5)
			uphy_bpmp_pcie_controller_state_set(pcie->cid, false);
		if (pcie->ep_mode_slot_supplies_en)
			disable_slot_regulators(pcie);
	}
	tegra_bwmgr_unregister(pcie->emc_bw);

	return 0;
}

static int tegra_pcie_dw_runtime_suspend(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);

	if (pcie->mode == DW_PCIE_EP_TYPE)
		return 0;

	tegra_pcie_downstream_dev_to_D0(pcie);

	dw_pcie_host_deinit(&pcie->pci.pp);

	if (pcie->is_safety_platform)
		clk_disable_unprepare(pcie->core_clk_m);

	tegra_pcie_dw_pme_turnoff(pcie);

	reset_control_assert(pcie->core_rst);
	tegra_pcie_disable_phy(pcie);
	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);
	regulator_disable(pcie->pex_ctl_reg);
	config_plat_gpio(pcie, 0);
	disable_slot_regulators(pcie);

	if (pcie->cid != CTRL_5)
		uphy_bpmp_pcie_controller_state_set(pcie->cid, false);

	return 0;
}

static int tegra_pcie_dw_runtime_resume(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	struct dw_pcie *pci = &pcie->pci;
	struct pcie_port *pp = &pci->pp;
	int ret = 0;
	u32 val;

	if (pcie->mode == DW_PCIE_EP_TYPE)
		return 0;

	if (pcie->cid != CTRL_5) {
		ret = uphy_bpmp_pcie_controller_state_set(pcie->cid, true);
		if (ret) {
			dev_err(pcie->dev, "Enabling controller-%d failed:%d\n",
				pcie->cid, ret);
			return ret;
		}
	}

	ret = enable_slot_regulators(pcie);
	if (ret)
		goto fail_slot_reg_en;

	config_plat_gpio(pcie, 1);

	ret = regulator_enable(pcie->pex_ctl_reg);
	if (ret < 0) {
		dev_err(pcie->dev, "regulator enable failed: %d\n", ret);
		goto fail_reg_en;
	}

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(pcie->dev, "Failed to enable core clock\n");
		goto fail_core_clk;
	}

	reset_control_deassert(pcie->core_apb_rst);

	ret = tegra_pcie_enable_phy(pcie);
	if (ret) {
		dev_err(pcie->dev, "failed to enable phy\n");
		goto fail_phy;
	}

	/* update CFG base address */
	writel(pcie->dbi_res->start & APPL_CFG_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_BASE_ADDR);

	/* configure this core for RP mode operation */
	writel(APPL_DM_TYPE_RP, pcie->appl_base + APPL_DM_TYPE);

	writel(0x0, pcie->appl_base + APPL_CFG_SLCG_OVERRIDE);

	val = readl(pcie->appl_base + APPL_CTRL);
	writel(val | APPL_CTRL_SYS_PRE_DET_STATE, pcie->appl_base + APPL_CTRL);

	val = readl(pcie->appl_base + APPL_CFG_MISC);
	val |= APPL_CFG_MISC_SLV_EP_MODE;
	val |= (APPL_CFG_MISC_ARCACHE_VAL << APPL_CFG_MISC_ARCACHE_SHIFT);
	writel(val, pcie->appl_base + APPL_CFG_MISC);

	if (pcie->disable_clock_request) {
		val = readl(pcie->appl_base + APPL_PINMUX);
		val |= APPL_PINMUX_CLKREQ_OVERRIDE_EN;
		val &= ~APPL_PINMUX_CLKREQ_OVERRIDE;
		writel(val, pcie->appl_base + APPL_PINMUX);
	}

	/* update iATU_DMA base address */
	writel(pcie->atu_dma_res->start & APPL_CFG_IATU_DMA_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_IATU_DMA_BASE_ADDR);

	reset_control_deassert(pcie->core_rst);

	if (pcie->disable_clock_request) {
		/* Disable ASPM-L1SS adv as there is no CLKREQ routing */
		disable_aspm_l11(pcie); /* Disable L1.1 */
		disable_aspm_l12(pcie); /* Disable L1.2 */
	}

	/* program to use MPS of 256 whereever possible */
	pcie_bus_config = PCIE_BUS_SAFE;

	pp->root_bus_nr = -1;
	pp->ops = &tegra_pcie_dw_host_ops;

	/* Disable MSI interrupts for PME messages */
	pcie_pme_disable_msi();

	ret = dw_pcie_host_init(pp);
	if (ret < 0) {
		dev_err(pcie->dev, "PCIE : Add PCIe port failed: %d\n", ret);
		goto fail_host_init;
	}

	return 0;

fail_host_init:
	reset_control_assert(pcie->core_rst);
	tegra_pcie_disable_phy(pcie);
fail_phy:
	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);
fail_core_clk:
	regulator_disable(pcie->pex_ctl_reg);
fail_reg_en:
	config_plat_gpio(pcie, 0);
	disable_slot_regulators(pcie);
fail_slot_reg_en:
	if (pcie->cid != CTRL_5)
		uphy_bpmp_pcie_controller_state_set(pcie->cid, false);

	return ret;
}

static int tegra_pcie_dw_suspend_late(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	u32 val;

	if (pcie->mode == DW_PCIE_EP_TYPE) {
		dev_err(dev, "Tegra PCIe is in EP mode, suspend not allowed");
		return -EPERM;
	}

	if (!pcie->link_state && pcie->power_down_en)
		return 0;

	/* Enable HW_HOT_RST mode */
	val = readl(pcie->appl_base + APPL_CTRL);
	val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
		  APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
	val |= APPL_CTRL_HW_HOT_RST_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	return 0;
}

static int tegra_pcie_dw_suspend_noirq(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	int ret = 0;

	if (!pcie->link_state && pcie->power_down_en)
		return 0;

	/* save MSI interrutp vector*/
	dw_pcie_read(pcie->pci.dbi_base + PORT_LOGIC_MSI_CTRL_INT_0_EN, 4,
		     &pcie->msi_ctrl_int);
	if (pcie->is_safety_platform)
		clk_disable_unprepare(pcie->core_clk_m);
	tegra_pcie_downstream_dev_to_D0(pcie);
	tegra_pcie_dw_pme_turnoff(pcie);
	reset_control_assert(pcie->core_rst);
	tegra_pcie_disable_phy(pcie);
	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);
	regulator_disable(pcie->pex_ctl_reg);
	config_plat_gpio(pcie, 0);
	disable_slot_regulators(pcie);
	if (pcie->cid != CTRL_5) {
		ret = uphy_bpmp_pcie_controller_state_set(pcie->cid, false);
		if (ret) {
			dev_err(pcie->dev, "Disabling ctrl-%d failed:%d\n",
				pcie->cid, ret);
			return ret;
		}
	}
	if (gpio_is_valid(pcie->pex_wake) && device_may_wakeup(dev)) {
		ret = enable_irq_wake(gpio_to_irq(pcie->pex_wake));
		if (ret < 0)
			dev_err(dev, "enable wake irq failed: %d\n", ret);
	}

	return ret;
}

static int tegra_pcie_dw_resume_noirq(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	int ret;
	u32 val;

	if (!pcie->link_state && pcie->power_down_en)
		return 0;

	if (gpio_is_valid(pcie->pex_wake) && device_may_wakeup(dev)) {
		ret = disable_irq_wake(gpio_to_irq(pcie->pex_wake));
		if (ret < 0)
			dev_err(dev, "disable wake irq failed: %d\n", ret);
	}

	if (pcie->cid != CTRL_5) {
		ret = uphy_bpmp_pcie_controller_state_set(pcie->cid, true);
		if (ret) {
			dev_err(pcie->dev, "Enabling controller-%d failed:%d\n",
				pcie->cid, ret);
			return ret;
		}
	}

	ret = enable_slot_regulators(pcie);
	if (ret)
		goto fail_slot_reg_en;

	config_plat_gpio(pcie, 1);

	ret = regulator_enable(pcie->pex_ctl_reg);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed: %d\n", ret);
		goto fail_reg_en;
	}

	if (pcie->tsa_config_addr) {
		void __iomem *tsa_addr;

		tsa_addr = ioremap(pcie->tsa_config_addr, 4);
		val = readl(tsa_addr);
		val |= TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_HUB2 <<
		       TSA_CONFIG_STATIC0_CSW_PCIE5W_0_SO_DEV_HUBID_SHIFT;
		writel(val, tsa_addr);
		iounmap(tsa_addr);
	}

	ret = clk_prepare_enable(pcie->core_clk);
	if (ret) {
		dev_err(dev, "Failed to enable core clock\n");
		goto fail_core_clk;
	}
	reset_control_deassert(pcie->core_apb_rst);
	ret = tegra_pcie_enable_phy(pcie);
	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		goto fail_phy;
	}

	/* Enable HW_HOT_RST mode */
	val = readl(pcie->appl_base + APPL_CTRL);
	val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
		  APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
	val |= APPL_CTRL_HW_HOT_RST_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	writel(pcie->dbi_res->start & APPL_CFG_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_BASE_ADDR);

	/* configure this core for RP mode operation */
	writel(APPL_DM_TYPE_RP, pcie->appl_base + APPL_DM_TYPE);

	writel(0x0, pcie->appl_base + APPL_CFG_SLCG_OVERRIDE);

	val = readl(pcie->appl_base + APPL_CTRL);
	writel(val | APPL_CTRL_SYS_PRE_DET_STATE, pcie->appl_base + APPL_CTRL);

	val = readl(pcie->appl_base + APPL_CFG_MISC);
	val |= APPL_CFG_MISC_SLV_EP_MODE;
	val |= (APPL_CFG_MISC_ARCACHE_VAL << APPL_CFG_MISC_ARCACHE_SHIFT);
	writel(val, pcie->appl_base + APPL_CFG_MISC);

	if (pcie->enable_srns) {
		/* Cut the REFCLK to EP as it is using its internal clock */
		val = readl(pcie->appl_base + APPL_PINMUX);
		val |= APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE_EN;
		val &= ~APPL_PINMUX_CLK_OUTPUT_IN_OVERRIDE;
		writel(val, pcie->appl_base + APPL_PINMUX);
	}

	if (pcie->disable_clock_request) {
		val = readl(pcie->appl_base + APPL_PINMUX);
		val |= APPL_PINMUX_CLKREQ_OVERRIDE_EN;
		val &= ~APPL_PINMUX_CLKREQ_OVERRIDE;
		writel(val, pcie->appl_base + APPL_PINMUX);
	}

	/* update iATU_DMA base address */
	writel(pcie->atu_dma_res->start & APPL_CFG_IATU_DMA_BASE_ADDR_MASK,
	       pcie->appl_base + APPL_CFG_IATU_DMA_BASE_ADDR);

	reset_control_deassert(pcie->core_rst);

	if (pcie->disable_clock_request) {
		/* Disable ASPM-L1SS adv as there is no CLKREQ routing */
		disable_aspm_l11(pcie); /* Disable L1.1 */
		disable_aspm_l12(pcie); /* Disable L1.2 */
	}

	tegra_pcie_dw_host_init(&pcie->pci.pp);

	/* restore MSI interrutp vector*/
	dw_pcie_write(pcie->pci.dbi_base + PORT_LOGIC_MSI_CTRL_INT_0_EN, 4,
		      pcie->msi_ctrl_int);

	tegra_pcie_dw_scan_bus(&pcie->pci.pp);

	return 0;
fail_phy:
	reset_control_assert(pcie->core_apb_rst);
	clk_disable_unprepare(pcie->core_clk);
fail_core_clk:
	regulator_disable(pcie->pex_ctl_reg);
fail_reg_en:
	config_plat_gpio(pcie, 0);
	disable_slot_regulators(pcie);
fail_slot_reg_en:
	if (pcie->cid != CTRL_5) {
		ret = uphy_bpmp_pcie_controller_state_set(pcie->cid, false);
		if (ret) {
			dev_err(pcie->dev,
				"Disabling controller-%d failed:%d\n",
				pcie->cid, ret);
			return ret;
		}
	}
	return ret;
}

static int tegra_pcie_dw_resume_early(struct device *dev)
{
	struct tegra_pcie_dw *pcie = dev_get_drvdata(dev);
	u32 val;

	if (!pcie->link_state && pcie->power_down_en)
		return 0;

	/* Disable HW_HOT_RST mode */
	val = readl(pcie->appl_base + APPL_CTRL);
	val &= ~(APPL_CTRL_HW_HOT_RST_MODE_MASK <<
		  APPL_CTRL_HW_HOT_RST_MODE_SHIFT);
	val |= APPL_CTRL_HW_HOT_RST_MODE_IMDT_RST <<
		APPL_CTRL_HW_HOT_RST_MODE_SHIFT;
	val &= ~APPL_CTRL_HW_HOT_RST_EN;
	writel(val, pcie->appl_base + APPL_CTRL);

	return 0;
}

static void tegra_pcie_dw_shutdown(struct platform_device *pdev)
{
	struct tegra_pcie_dw *pcie = platform_get_drvdata(pdev);

	if (pcie->mode == DW_PCIE_RC_TYPE) {
		if (!pcie->link_state && pcie->power_down_en)
			return;

		destroy_dma_test_debugfs(pcie);
		debugfs_remove_recursive(pcie->debugfs);
		tegra_pcie_downstream_dev_to_D0(pcie);

		if (pcie->is_safety_platform)
			clk_disable_unprepare(pcie->core_clk_m);

	/* Disable interrupts */
	disable_irq(pcie->pci.pp.irq);
	if (IS_ENABLED(CONFIG_PCI_MSI))
		disable_irq(pcie->pci.pp.msi_irq);

	tegra_pcie_dw_pme_turnoff(pcie);

		reset_control_assert(pcie->core_rst);
		tegra_pcie_disable_phy(pcie);
		reset_control_assert(pcie->core_apb_rst);
		clk_disable_unprepare(pcie->core_clk);
		regulator_disable(pcie->pex_ctl_reg);
		config_plat_gpio(pcie, 0);
		disable_slot_regulators(pcie);

		if (pcie->cid != CTRL_5)
			uphy_bpmp_pcie_controller_state_set(pcie->cid, false);

	} else if (pcie->mode == DW_PCIE_EP_TYPE) {
		if (!kfifo_put(&pcie->event_fifo, EP_EVENT_EXIT))
			dev_err(pcie->dev, "EVENT: fifo is full\n");
		kthread_stop(pcie->pcie_ep_task);

		tegra_pcie_disable_phy(pcie);

		regulator_disable(pcie->pex_ctl_reg);

		if (pcie->cid != CTRL_5)
			uphy_bpmp_pcie_controller_state_set(pcie->cid, false);
	}
	tegra_bwmgr_unregister(pcie->emc_bw);
}

static const struct dev_pm_ops tegra_pcie_dw_pm_ops = {
	.suspend_late = tegra_pcie_dw_suspend_late,
	.suspend_noirq = tegra_pcie_dw_suspend_noirq,
	.resume_noirq = tegra_pcie_dw_resume_noirq,
	.resume_early = tegra_pcie_dw_resume_early,
	.runtime_suspend = tegra_pcie_dw_runtime_suspend,
	.runtime_resume = tegra_pcie_dw_runtime_resume,
};

static struct platform_driver tegra_pcie_dw_driver = {
	.probe = tegra_pcie_dw_probe,
	.remove = tegra_pcie_dw_remove,
	.shutdown = tegra_pcie_dw_shutdown,
	.driver = {
		.name	= "tegra-pcie-dw",
#ifdef CONFIG_PM
		.pm = &tegra_pcie_dw_pm_ops,
#endif
		.of_match_table = tegra_pcie_dw_of_match,
	},
};

static int __init tegra_pcie_rp_init(void)
{
	return platform_driver_register(&tegra_pcie_dw_driver);
}

#if IS_MODULE(CONFIG_PCIE_TEGRA)
static void __exit tegra_pcie_rp_deinit(void)
{
	platform_driver_unregister(&tegra_pcie_dw_driver);
}

module_init(tegra_pcie_rp_init);
module_exit(tegra_pcie_rp_deinit);
#else
late_initcall(tegra_pcie_rp_init);
#endif

MODULE_AUTHOR("Vidya Sagar <vidyas@nvidia.com>");
MODULE_DESCRIPTION("Nvidia PCIe host controller driver");
MODULE_LICENSE("GPL v2");
