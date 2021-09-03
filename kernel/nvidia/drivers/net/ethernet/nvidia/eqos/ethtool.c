/* =========================================================================
 * The Synopsys DWC ETHER QOS Software Driver and documentation (hereinafter
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto.  Permission is hereby granted,
 * free of charge, to any person obtaining a copy of this software annotated
 * with this license and the Software, to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * =========================================================================
 */
/*
 * Copyright (c) 2015-2021, NVIDIA CORPORATION.  All rights reserved.
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
/*!@file: eqos_ethtool.c
 * @brief: Driver functions.
 */
#include "yheader.h"
#include "ethtool.h"

struct eqos_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

/* HW extra status */
#define EQOS_EXTRA_STAT(m) \
	{#m, FIELD_SIZEOF(struct eqos_extra_stats, m), \
	offsetof(struct eqos_prv_data, xstats.m)}

static const struct eqos_stats eqos_gstrings_stats[] = {
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[0]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[1]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[2]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[3]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[4]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[5]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[6]),
	EQOS_EXTRA_STAT(q_re_alloc_rx_buf_failed[7]),

	/* Tx/Rx IRQ error info */
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[0]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[1]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[2]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[3]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[4]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[5]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[6]),
	EQOS_EXTRA_STAT(tx_process_stopped_irq_n[7]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[0]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[1]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[2]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[3]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[4]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[5]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[6]),
	EQOS_EXTRA_STAT(rx_process_stopped_irq_n[7]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[0]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[1]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[2]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[3]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[4]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[5]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[6]),
	EQOS_EXTRA_STAT(tx_buf_unavailable_irq_n[7]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[0]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[1]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[2]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[3]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[4]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[5]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[6]),
	EQOS_EXTRA_STAT(rx_buf_unavailable_irq_n[7]),
	EQOS_EXTRA_STAT(rx_watchdog_irq_n),
	EQOS_EXTRA_STAT(fatal_bus_error_irq_n),
	EQOS_EXTRA_STAT(pmt_irq_n),
	/* Tx/Rx IRQ Events */
	EQOS_EXTRA_STAT(tx_normal_irq_n[0]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[1]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[2]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[3]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[4]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[5]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[6]),
	EQOS_EXTRA_STAT(tx_normal_irq_n[7]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[0]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[1]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[2]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[3]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[4]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[5]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[6]),
	EQOS_EXTRA_STAT(rx_normal_irq_n[7]),
	EQOS_EXTRA_STAT(napi_poll_n),
	EQOS_EXTRA_STAT(tx_clean_n[0]),
	EQOS_EXTRA_STAT(tx_clean_n[1]),
	EQOS_EXTRA_STAT(tx_clean_n[2]),
	EQOS_EXTRA_STAT(tx_clean_n[3]),
	EQOS_EXTRA_STAT(tx_clean_n[4]),
	EQOS_EXTRA_STAT(tx_clean_n[5]),
	EQOS_EXTRA_STAT(tx_clean_n[6]),
	EQOS_EXTRA_STAT(tx_clean_n[7]),
	/* EEE */
	EQOS_EXTRA_STAT(tx_path_in_lpi_mode_irq_n),
	EQOS_EXTRA_STAT(tx_path_exit_lpi_mode_irq_n),
	EQOS_EXTRA_STAT(rx_path_in_lpi_mode_irq_n),
	EQOS_EXTRA_STAT(rx_path_exit_lpi_mode_irq_n),
	/* Tx/Rx frames */
	EQOS_EXTRA_STAT(tx_pkt_n),
	EQOS_EXTRA_STAT(rx_pkt_n),
	EQOS_EXTRA_STAT(tx_vlan_pkt_n),
	EQOS_EXTRA_STAT(rx_vlan_pkt_n),
	EQOS_EXTRA_STAT(tx_timestamp_captured_n),
	EQOS_EXTRA_STAT(rx_timestamp_captured_n),
	EQOS_EXTRA_STAT(tx_tso_pkt_n),

	/* Tx/Rx frames per channels/queues */
	EQOS_EXTRA_STAT(q_tx_pkt_n[0]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[1]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[2]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[3]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[4]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[5]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[6]),
	EQOS_EXTRA_STAT(q_tx_pkt_n[7]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[0]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[1]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[2]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[3]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[4]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[5]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[6]),
	EQOS_EXTRA_STAT(q_rx_pkt_n[7]),
	EQOS_EXTRA_STAT(link_disconnect_count),
	EQOS_EXTRA_STAT(link_connect_count),
};

#define EQOS_EXTRA_STAT_LEN ARRAY_SIZE(eqos_gstrings_stats)

/* HW MAC Management counters (if supported) */
#define EQOS_MMC_STAT(m)	\
	{ #m, FIELD_SIZEOF(struct eqos_mmc_counters, m),	\
	offsetof(struct eqos_prv_data, mmc.m)}

static const struct eqos_stats eqos_mmc[] = {
	/* MMC TX counters */
	EQOS_MMC_STAT(mmc_tx_octetcount_gb),
	EQOS_MMC_STAT(mmc_tx_framecount_gb),
	EQOS_MMC_STAT(mmc_tx_broadcastframe_g),
	EQOS_MMC_STAT(mmc_tx_multicastframe_g),
	EQOS_MMC_STAT(mmc_tx_64_octets_gb),
	EQOS_MMC_STAT(mmc_tx_65_to_127_octets_gb),
	EQOS_MMC_STAT(mmc_tx_128_to_255_octets_gb),
	EQOS_MMC_STAT(mmc_tx_256_to_511_octets_gb),
	EQOS_MMC_STAT(mmc_tx_512_to_1023_octets_gb),
	EQOS_MMC_STAT(mmc_tx_1024_to_max_octets_gb),
	EQOS_MMC_STAT(mmc_tx_unicast_gb),
	EQOS_MMC_STAT(mmc_tx_multicast_gb),
	EQOS_MMC_STAT(mmc_tx_broadcast_gb),
	EQOS_MMC_STAT(mmc_tx_underflow_error),
	EQOS_MMC_STAT(mmc_tx_singlecol_g),
	EQOS_MMC_STAT(mmc_tx_multicol_g),
	EQOS_MMC_STAT(mmc_tx_deferred),
	EQOS_MMC_STAT(mmc_tx_latecol),
	EQOS_MMC_STAT(mmc_tx_exesscol),
	EQOS_MMC_STAT(mmc_tx_carrier_error),
	EQOS_MMC_STAT(mmc_tx_octetcount_g),
	EQOS_MMC_STAT(mmc_tx_framecount_g),
	EQOS_MMC_STAT(mmc_tx_excessdef),
	EQOS_MMC_STAT(mmc_tx_pause_frame),
	EQOS_MMC_STAT(mmc_tx_vlan_frame_g),

	/* MMC RX counters */
	EQOS_MMC_STAT(mmc_rx_framecount_gb),
	EQOS_MMC_STAT(mmc_rx_octetcount_gb),
	EQOS_MMC_STAT(mmc_rx_octetcount_g),
	EQOS_MMC_STAT(mmc_rx_broadcastframe_g),
	EQOS_MMC_STAT(mmc_rx_multicastframe_g),
	EQOS_MMC_STAT(mmc_rx_crc_error),
	EQOS_MMC_STAT(mmc_rx_align_error),
	EQOS_MMC_STAT(mmc_rx_run_error),
	EQOS_MMC_STAT(mmc_rx_jabber_error),
	EQOS_MMC_STAT(mmc_rx_undersize_g),
	EQOS_MMC_STAT(mmc_rx_oversize_g),
	EQOS_MMC_STAT(mmc_rx_64_octets_gb),
	EQOS_MMC_STAT(mmc_rx_65_to_127_octets_gb),
	EQOS_MMC_STAT(mmc_rx_128_to_255_octets_gb),
	EQOS_MMC_STAT(mmc_rx_256_to_511_octets_gb),
	EQOS_MMC_STAT(mmc_rx_512_to_1023_octets_gb),
	EQOS_MMC_STAT(mmc_rx_1024_to_max_octets_gb),
	EQOS_MMC_STAT(mmc_rx_unicast_g),
	EQOS_MMC_STAT(mmc_rx_length_error),
	EQOS_MMC_STAT(mmc_rx_outofrangetype),
	EQOS_MMC_STAT(mmc_rx_pause_frames),
	EQOS_MMC_STAT(mmc_rx_fifo_overflow),
	EQOS_MMC_STAT(mmc_rx_vlan_frames_gb),
	EQOS_MMC_STAT(mmc_rx_watchdog_error),

	/* IPC */
	EQOS_MMC_STAT(mmc_rx_ipc_intr_mask),
	EQOS_MMC_STAT(mmc_rx_ipc_intr),

	/* IPv4 */
	EQOS_MMC_STAT(mmc_rx_ipv4_gd),
	EQOS_MMC_STAT(mmc_rx_ipv4_hderr),
	EQOS_MMC_STAT(mmc_rx_ipv4_nopay),
	EQOS_MMC_STAT(mmc_rx_ipv4_frag),
	EQOS_MMC_STAT(mmc_rx_ipv4_udsbl),

	/* IPV6 */
	EQOS_MMC_STAT(mmc_rx_ipv6_gd_octets),
	EQOS_MMC_STAT(mmc_rx_ipv6_hderr_octets),
	EQOS_MMC_STAT(mmc_rx_ipv6_nopay_octets),

	/* Protocols */
	EQOS_MMC_STAT(mmc_rx_udp_gd),
	EQOS_MMC_STAT(mmc_rx_udp_err),
	EQOS_MMC_STAT(mmc_rx_tcp_gd),
	EQOS_MMC_STAT(mmc_rx_tcp_err),
	EQOS_MMC_STAT(mmc_rx_icmp_gd),
	EQOS_MMC_STAT(mmc_rx_icmp_err),

	/* IPv4 */
	EQOS_MMC_STAT(mmc_rx_ipv4_gd_octets),
	EQOS_MMC_STAT(mmc_rx_ipv4_hderr_octets),
	EQOS_MMC_STAT(mmc_rx_ipv4_nopay_octets),
	EQOS_MMC_STAT(mmc_rx_ipv4_frag_octets),
	EQOS_MMC_STAT(mmc_rx_ipv4_udsbl_octets),

	/* IPV6 */
	EQOS_MMC_STAT(mmc_rx_ipv6_gd),
	EQOS_MMC_STAT(mmc_rx_ipv6_hderr),
	EQOS_MMC_STAT(mmc_rx_ipv6_nopay),

	/* Protocols */
	EQOS_MMC_STAT(mmc_rx_udp_gd_octets),
	EQOS_MMC_STAT(mmc_rx_udp_err_octets),
	EQOS_MMC_STAT(mmc_rx_tcp_gd_octets),
	EQOS_MMC_STAT(mmc_rx_tcp_err_octets),
	EQOS_MMC_STAT(mmc_rx_icmp_gd_octets),
	EQOS_MMC_STAT(mmc_rx_icmp_err_octets),
};

#define EQOS_MMC_STATS_LEN ARRAY_SIZE(eqos_mmc)

static int eqos_get_ts_info(struct net_device *net,
			    struct ethtool_ts_info *info)
{
	info->so_timestamping =
	    SOF_TIMESTAMPING_TX_SOFTWARE |
	    SOF_TIMESTAMPING_RX_SOFTWARE |
	    SOF_TIMESTAMPING_SOFTWARE |
	    SOF_TIMESTAMPING_TX_HARDWARE |
	    SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = 0;

	info->tx_types = (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);

	info->rx_filters = 1 << HWTSTAMP_FILTER_NONE;
	info->rx_filters |=
	    (1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
	    (1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
	    (1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
	    (1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
	    (1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
	    (1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
	    (1 << HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

static const struct ethtool_ops eqos_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_pauseparam = eqos_get_pauseparam,
	.set_pauseparam = eqos_set_pauseparam,
	.get_wol = eqos_get_wol,
	.set_wol = eqos_set_wol,
	.get_coalesce = eqos_get_coalesce,
	.set_coalesce = eqos_set_coalesce,
	.get_ethtool_stats = eqos_get_ethtool_stats,
	.get_strings = eqos_get_strings,
	.get_sset_count = eqos_get_sset_count,
	.get_ts_info = eqos_get_ts_info,
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 9, 0)
	.get_link_ksettings = eqos_get_link_ksettings,
	.set_link_ksettings = eqos_set_link_ksettings,
#else
	.get_settings = eqos_getsettings,
	.set_settings = eqos_setsettings,
#endif
};

struct ethtool_ops *eqos_get_ethtool_ops(void)
{
	return (struct ethtool_ops *)&eqos_ethtool_ops;
}

/*!
 * \details This function is invoked by kernel when user request to get the
 * pause parameters through standard ethtool command.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] Pause – pointer to ethtool_pauseparam structure.
 *
 * \return void
 */

static void eqos_get_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	struct hw_if_struct *hw_if = &(pdata->hw_if);
	struct phy_device *phydev = pdata->phydev;
	unsigned int data;

	pr_debug("-->eqos_get_pauseparam\n");

	pause->rx_pause = 0;
	pause->tx_pause = 0;

	if (pdata->hw_feat.pcs_sel) {
		pause->autoneg = 1;
		data = hw_if->get_an_adv_pause_param();
		if (!(data == 1) && !(data == 2))
			return;
	} else {
		pause->autoneg = pdata->phydev->autoneg;

		/* return if PHY doesn't support FLOW ctrl */
		if (!(phydev->supported & SUPPORTED_Pause) ||
		    !(phydev->supported & SUPPORTED_Asym_Pause))
			return;
	}

	if ((pdata->flow_ctrl & EQOS_FLOW_CTRL_RX) == EQOS_FLOW_CTRL_RX)
		pause->rx_pause = 1;

	if ((pdata->flow_ctrl & EQOS_FLOW_CTRL_TX) == EQOS_FLOW_CTRL_TX)
		pause->tx_pause = 1;

	pr_debug("<--eqos_get_pauseparam\n");
}

/*!
 * \details This function is invoked by kernel when user request to set the
 * pause parameters through standard ethtool command.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] pause – pointer to ethtool_pauseparam structure.
 *
 * \return int
 *
 * \retval zero on success and -ve number on failure.
 */

static int eqos_set_pauseparam(struct net_device *dev,
			       struct ethtool_pauseparam *pause)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	struct hw_if_struct *hw_if = &(pdata->hw_if);
	struct phy_device *phydev = pdata->phydev;
	int new_pause = EQOS_FLOW_CTRL_OFF;
	unsigned int data;
	int ret = 0;

	if (pdata->dt_cfg.pause_frames == PAUSE_FRAMES_DISABLED)
		return -EOPNOTSUPP;

	pr_debug("-->eqos_set_pauseparam: "
	      "autoneg = %d tx_pause = %d rx_pause = %d\n",
	      pause->autoneg, pause->tx_pause, pause->rx_pause);

	/* return if PHY doesn't support FLOW ctrl */
	if (pdata->hw_feat.pcs_sel) {
		data = hw_if->get_an_adv_pause_param();
		if (!(data == 1) && !(data == 2))
			return -EINVAL;
	} else {
		if (!(phydev->supported & SUPPORTED_Pause) ||
		    !(phydev->supported & SUPPORTED_Asym_Pause))
			return -EINVAL;
	}

	if (pause->rx_pause)
		new_pause |= EQOS_FLOW_CTRL_RX;
	if (pause->tx_pause)
		new_pause |= EQOS_FLOW_CTRL_TX;

	if (new_pause == pdata->flow_ctrl && !pause->autoneg)
		return -EINVAL;

	pdata->flow_ctrl = new_pause;

	if (pdata->hw_feat.pcs_sel) {
		eqos_configure_flow_ctrl(pdata);
	} else {
		phydev->autoneg = pause->autoneg;
		if (phydev->autoneg) {
			if (netif_running(dev))
				ret = phy_start_aneg(phydev);
		} else {
			eqos_configure_flow_ctrl(pdata);
		}
	}

	pr_debug("<--eqos_set_pauseparam\n");

	return ret;
}

void eqos_configure_flow_ctrl(struct eqos_prv_data *pdata)
{
	struct hw_if_struct *hw_if = &(pdata->hw_if);
	UINT qinx;

	pr_debug("-->eqos_configure_flow_ctrl\n");

	if ((pdata->flow_ctrl & EQOS_FLOW_CTRL_RX) == EQOS_FLOW_CTRL_RX)
		hw_if->enable_rx_flow_ctrl();
	else
		hw_if->disable_rx_flow_ctrl();

	/* As ethtool does not provide queue level configuration
	 * Tx flow control is disabled/enabled for all transmit queues
	 */
	if ((pdata->flow_ctrl & EQOS_FLOW_CTRL_TX) == EQOS_FLOW_CTRL_TX) {
		for (qinx = 0; qinx < EQOS_TX_QUEUE_CNT; qinx++)
			hw_if->enable_tx_flow_ctrl(qinx);
	} else {
		for (qinx = 0; qinx < EQOS_TX_QUEUE_CNT; qinx++)
			hw_if->disable_tx_flow_ctrl(qinx);
	}

	pdata->oldflow_ctrl = pdata->flow_ctrl;

	pr_debug("<--eqos_configure_flow_ctrl\n");
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 9, 0)
static int eqos_get_link_ksettings(struct net_device *dev,
                                   struct ethtool_link_ksettings *cmd)
{
        struct eqos_prv_data *pdata = netdev_priv(dev);

        if (!netif_running(dev))
                return -EINVAL;

        if (!pdata->phydev)
                return -ENODEV;

        phy_ethtool_ksettings_get(pdata->phydev, cmd);

        return 0;
}

static int eqos_set_link_ksettings(struct net_device *dev,
                                   const struct ethtool_link_ksettings *cmd)
{
        struct eqos_prv_data *pdata = netdev_priv(dev);

        return phy_ethtool_ksettings_set(pdata->phydev, cmd);
}

#else

/*!
 * \details This function is invoked by kernel when user request to get the
 * various device settings through standard ethtool command. This function
 * support to get the PHY related settings like link status, interface type,
 * auto-negotiation parameters and pause parameters etc.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] cmd – pointer to ethtool_cmd structure.
 *
 * \return int
 *
 * \retval zero on success and -ve number on failure.
 */
static int eqos_getsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	int ret = 0;

	if (!netif_running(dev))
		return -EINVAL;

	if (!pdata->phydev)
		return -ENODEV;

	cmd->transceiver = XCVR_EXTERNAL;

	spin_lock_irq(&pdata->lock);
	ret = phy_ethtool_gset(pdata->phydev, cmd);
	spin_unlock_irq(&pdata->lock);

	return ret;
}

/*!
 * \details This function is invoked by kernel when user request to set the
 * various device settings through standard ethtool command. This function
 * support to set the PHY related settings like link status, interface type,
 * auto-negotiation parameters and pause parameters etc.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] cmd – pointer to ethtool_cmd structure.
 *
 * \return int
 *
 * \retval zero on success and -ve number on failure.
 */
static int eqos_setsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	u8 duplex = cmd->duplex;
	int ret = 0;

	if (pdata->num_chans == MAX_CHANS &&
	    duplex == DUPLEX_HALF) {
		netdev_err(dev, "Half duplex mode not allowed in multi-channel\n");
		return -ENOTSUPP;
	}

	spin_lock_irq(&pdata->lock);
	ret = phy_ethtool_sset(pdata->phydev, cmd);
	spin_unlock_irq(&pdata->lock);

	return ret;
}
#endif

/*!
 * \details This function is invoked by kernel when user request to get report
 * whether wake-on-lan is enable or not.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] wol – pointer to ethtool_wolinfo structure.
 *
 * \return void
 */

static void eqos_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);

	wol->supported = 0;
	wol->wolopts = 0;

	if (pdata->phydev)
		phy_ethtool_get_wol(pdata->phydev, wol);
}

/*!
 * \details This function is invoked by kernel when user request to set
 * pmt parameters for remote wakeup or magic wakeup
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] wol – pointer to ethtool_wolinfo structure.
 *
 * \return int
 *
 * \retval zero on success and -ve number on failure.
 */

static int eqos_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	int ret;

	if (!pdata->phydev)
		return -ENOTSUPP;

	ret = phy_ethtool_set_wol(pdata->phydev, wol);
	if (ret < 0)
		return ret;

	/* Save WoL state */
	if (wol->wolopts & WAKE_MAGIC)
		pdata->wolopts = 1;
	else
		pdata->wolopts = 0;

	device_init_wakeup(&dev->dev, true);

	return ret;
}

u32 eqos_usec2riwt(u32 usec, struct eqos_prv_data *pdata)
{
	u32 ret = 0;

	pr_debug("-->eqos_usec2riwt\n");

	/* Eg:
	 * AXI System clock is 125 MHz, each clock cycle would then be 8ns
	 * For value 0x1 in watchdog timer, device would wait for 256
	 * clock cycles,
	 * ie, (8ns x 256) => 2.048us (rounding off to 2us)
	 * So below is the formula with above values
	 */

	ret = (usec * (EQOS_AXI_CLOCK / 1000000)) / 256;

	pr_debug("<--eqos_usec2riwt\n");

	return ret;
}

static u32 eqos_riwt2usec(u32 riwt, struct eqos_prv_data *pdata)
{
	u32 ret = 0;

	pr_debug("-->eqos_riwt2usec\n");

	/* using formula from 'eqos_usec2riwt' */
	ret = (riwt * 256) / (EQOS_AXI_CLOCK / 1000000);

	pr_debug("<--eqos_riwt2usec\n");

	return ret;
}

/*!
 * \details This function is invoked by kernel when user request to get
 * interrupt coalescing parameters. As coalescing parameters are same
 * for all the channels, so this function will get coalescing
 * details from channel zero and return.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] wol – pointer to ethtool_coalesce structure.
 *
 * \return int
 *
 * \retval 0
 */

static int eqos_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ec)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	struct rx_ring *prx_ring = GET_RX_WRAPPER_DESC(0);
	struct tx_ring *ptx_ring = GET_TX_WRAPPER_DESC(0);

	pr_debug("-->eqos_get_coalesce\n");

	memset(ec, 0, sizeof(struct ethtool_coalesce));

	ec->rx_coalesce_usecs = eqos_riwt2usec(prx_ring->rx_riwt, pdata);
	ec->rx_max_coalesced_frames = prx_ring->rx_coal_frames;

	ec->rx_coalesce_usecs = eqos_riwt2usec(prx_ring->rx_riwt, pdata);
	ec->rx_max_coalesced_frames = prx_ring->rx_coal_frames;

	if (ptx_ring->use_tx_usecs)
		ec->tx_coalesce_usecs = ptx_ring->tx_usecs;
	if (ptx_ring->use_tx_frames)
		ec->tx_max_coalesced_frames = ptx_ring->tx_coal_frames;

	pr_debug("<--eqos_get_coalesce\n");

	return 0;
}

/*!
 * \details This function is invoked by kernel when user request to set
 * interrupt coalescing parameters. This driver maintains same coalescing
 * parameters for all the channels, hence same changes will be applied to
 * all the channels.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] wol – pointer to ethtool_coalesce structure.
 *
 * \return int
 *
 * \retval zero on success and -ve number on failure.
 */

static int eqos_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *ec)
{
	unsigned int rx_riwt, rx_usec, qinx;
	bool use_tx_usecs = EQOS_COAELSCING_DISABLE;
	bool use_tx_frames = EQOS_COAELSCING_DISABLE;
	bool use_rx_usecs = EQOS_COAELSCING_DISABLE;
	bool use_rx_frames = EQOS_COAELSCING_DISABLE;
	struct eqos_prv_data *pdata = netdev_priv(dev);
	struct rx_ring *prx_ring = GET_RX_WRAPPER_DESC(0);
	struct tx_ring *ptx_ring = GET_TX_WRAPPER_DESC(0);

	pr_debug("-->eqos_set_coalesce\n");

	/* Check for not supported parameters  */
	if ((ec->rx_coalesce_usecs_irq) ||
	    (ec->rx_max_coalesced_frames_irq) || (ec->tx_coalesce_usecs_irq) ||
	    (ec->use_adaptive_rx_coalesce) || (ec->use_adaptive_tx_coalesce) ||
	    (ec->pkt_rate_low) || (ec->rx_coalesce_usecs_low) ||
	    (ec->rx_max_coalesced_frames_low) || (ec->tx_coalesce_usecs_high) ||
	    (ec->tx_max_coalesced_frames_low) || (ec->pkt_rate_high) ||
	    (ec->tx_coalesce_usecs_low) || (ec->rx_coalesce_usecs_high) ||
	    (ec->rx_max_coalesced_frames_high) ||
	    (ec->tx_max_coalesced_frames_irq) ||
	    (ec->stats_block_coalesce_usecs) ||
	    (ec->tx_max_coalesced_frames_high) || (ec->rate_sample_interval))
		return -EOPNOTSUPP;

	/* check if we are changing the parameters when interface is already up */
	if (prx_ring->rx_coal_frames != ec->rx_max_coalesced_frames
	    && netif_running(dev)) {
		DBGPR_ETHTOOL("Coalesce frame parameter can be changed only if interface is down\n");
		return -EINVAL;
	}

	if (ec->tx_coalesce_usecs !=  ptx_ring->tx_usecs &&
	    netif_running(dev)) {
		DBGPR_ETHTOOL("Coalesce Tx usec parameter can be changed only if interface is down\n");
		return -EINVAL;
	}

	if (ec->tx_max_coalesced_frames !=  ptx_ring->tx_coal_frames &&
	    netif_running(dev)) {
		DBGPR_ETHTOOL("Coalesce Tx frame parameter can be changed only if interface is down\n");
		return -EINVAL;
	}

	/* Enable Rx usec coalesing only if Rx-usecs is more than 3 usecs. */
	if (ec->rx_coalesce_usecs <= EQOS_MIN_RX_COALESCE_USEC)
		use_rx_usecs = EQOS_COAELSCING_DISABLE;
	else
		use_rx_usecs = EQOS_COAELSCING_ENABLE;

	DBGPR_ETHTOOL("RX COALESCING is %s\n",
		      (use_rx_usecs ? "ENABLED" : "DISABLED"));

	rx_riwt = eqos_usec2riwt(ec->rx_coalesce_usecs, pdata);

	/* Check the bounds of values for RX */
	if (rx_riwt > EQOS_MAX_DMA_RIWT) {
		rx_usec = eqos_riwt2usec(EQOS_MAX_DMA_RIWT, pdata);
		DBGPR_ETHTOOL("RX Coalesing is limited to %d usecs\n", rx_usec);
		return -EINVAL;
	} else
	if (ec->rx_max_coalesced_frames > RX_DESC_CNT) {
		DBGPR_ETHTOOL("RX Coalesing is limited to %d frames\n",
			      EQOS_RX_MAX_FRAMES);
		return -EINVAL;
	}
	if (ec->rx_max_coalesced_frames < EQOS_MIN_RX_COALESCE_FRAMES)
		use_rx_frames = EQOS_COAELSCING_DISABLE;
	else
		use_rx_frames = EQOS_COAELSCING_ENABLE;

	/*  On Rx side we support Rx_usecs and Rx-frames together only  */
	if (use_rx_frames && use_rx_usecs) {
		DBGPR_ETHTOOL("RX COALESCING is Enabled\n");
	} else if (!use_rx_frames && !use_rx_usecs) {
		DBGPR_ETHTOOL("RX COALESCING is Disabled\n");
	} else {
		DBGPR_ETHTOOL("Both Rx-frames and Rx-usecs need to be enabled or disabled together\n");
		return -EINVAL;
	}

	if (ec->tx_coalesce_usecs > EQOS_MAX_TX_COALESCE_USEC) {
		DBGPR_ETHTOOL("TX Coalesing is limited to %d usecs\n",
			      EQOS_MAX_TX_COALESCE_USEC);
		return -EINVAL;
	}

	if (ec->tx_max_coalesced_frames > EQOS_TX_MAX_FRAME) {
		DBGPR_ETHTOOL("TX Coalesing is limited to %d frames\n",
			      EQOS_TX_MAX_FRAME);
		return -EINVAL;
	}

	if (ec->tx_max_coalesced_frames < EQOS_MIN_TX_COALESCE_FRAMES) {
		DBGPR_ETHTOOL("TX-frames COALESCING is disabled\n");
		use_tx_frames = EQOS_COAELSCING_DISABLE;
	} else {
		use_tx_frames = EQOS_COAELSCING_ENABLE;
	}

	if (ec->tx_coalesce_usecs < EQOS_MIN_TX_COALESCE_USEC) {
		DBGPR_ETHTOOL("TX-usecs COALESCING is disabled\n");
		use_tx_usecs = EQOS_COAELSCING_DISABLE;
	} else {
		use_tx_usecs = EQOS_COAELSCING_ENABLE;
	}

	if (use_tx_frames && !use_tx_usecs) {
		DBGPR_ETHTOOL("Tx-usecs coalescing needs to be enabled if Tx-frames coalescing is enabled\n",
			      EQOS_TX_MAX_FRAME);
		return -EINVAL;
	}

	/* The selected parameters are applied to all the
	 * receive queues equally, so all the queue configurations
	 * are in sync. Update software data structure here. We cannot
	 * update hardware here since interface is down at this point.
	 * Hardware will be updated on interface getting up using
	 * "ifconfig eth0 up" after this setting is done.
	 */
	for (qinx = 0; qinx < EQOS_RX_QUEUE_CNT; qinx++) {
		prx_ring = GET_RX_WRAPPER_DESC(qinx);
		prx_ring->use_riwt = use_rx_usecs;
		prx_ring->rx_riwt = rx_riwt;
		prx_ring->rx_coal_frames = ec->rx_max_coalesced_frames;
	}
	for (qinx = 0; qinx < EQOS_TX_QUEUE_CNT; qinx++) {
		ptx_ring = GET_TX_WRAPPER_DESC(qinx);
		ptx_ring->tx_usecs = ec->tx_coalesce_usecs;
		ptx_ring->tx_coal_frames = ec->tx_max_coalesced_frames;
		ptx_ring->use_tx_usecs = use_tx_usecs;
		ptx_ring->use_tx_frames = use_tx_frames;
	}

	pr_debug("<--eqos_set_coalesce\n");

	return 0;
}

/*!
 * \details This function is invoked by kernel when user
 * requests to get the extended statistics about the device.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] data – pointer in which extended statistics
 *                   should be put.
 *
 * \return void
 */

static void eqos_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *dummy, u64 *data)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	int i, j = 0;

	pr_debug("-->eqos_get_ethtool_stats\n");

	if (pdata->hw_feat.mmc_sel) {
		eqos_mmc_read(&pdata->mmc);

		for (i = 0; i < EQOS_MMC_STATS_LEN; i++) {
			char *p = (char *)pdata + eqos_mmc[i].stat_offset;

			data[j++] = (eqos_mmc[i].sizeof_stat ==
				     sizeof(u64)) ? (*(u64 *) p) : (*(u32 *) p);
		}
	}

	for (i = 0; i < EQOS_EXTRA_STAT_LEN; i++) {
		char *p = (char *)pdata + eqos_gstrings_stats[i].stat_offset;
		data[j++] = (eqos_gstrings_stats[i].sizeof_stat ==
			     sizeof(u64)) ? (*(u64 *) p) : (*(u32 *) p);
	}

	pr_debug("<--eqos_get_ethtool_stats\n");
}

/*!
 * \details This function returns a set of strings that describe
 * the requested objects.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] data – pointer in which requested string should be put.
 *
 * \return void
 */

static void eqos_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	int i;
	u8 *p = data;

	pr_debug("-->eqos_get_strings\n");

	switch (stringset) {
	case ETH_SS_STATS:
		if (pdata->hw_feat.mmc_sel) {
			for (i = 0; i < EQOS_MMC_STATS_LEN; i++) {
				memcpy(p, eqos_mmc[i].stat_string,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < EQOS_EXTRA_STAT_LEN; i++) {
			memcpy(p, eqos_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		WARN_ON(1);
	}

	pr_debug("<--eqos_get_strings\n");
}

/*!
 * \details This function gets number of strings that @get_strings
 * will write.
 *
 * \param[in] dev – pointer to net device structure.
 *
 * \return int
 *
 * \retval +ve(>0) on success, 0 if that string is not
 * defined and -ve on failure.
 */

static int eqos_get_sset_count(struct net_device *dev, int sset)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);
	int len = 0;

	pr_debug("-->eqos_get_sset_count\n");

	switch (sset) {
	case ETH_SS_STATS:
		if (pdata->hw_feat.mmc_sel)
			len = EQOS_MMC_STATS_LEN;
		len += EQOS_EXTRA_STAT_LEN;
		break;
	default:
		len = -EOPNOTSUPP;
	}

	pr_debug("<--eqos_get_sset_count\n");

	return len;
}

/*!
 * \details This function is invoked by kernel when user
 * request to enable/disable tso feature.
 *
 * \param[in] dev – pointer to net device structure.
 * \param[in] data – 1/0 for enabling/disabling tso.
 *
 * \return int
 *
 * \retval 0 on success and -ve on failure.
 */
#if 0
static int eqos_set_tso(struct net_device *dev, u32 data)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);

	pr_debug("-->eqos_set_tso\n");

	if (pdata->hw_feat.tso_en == 0)
		return -EOPNOTSUPP;

	if (data)
		dev->features |= NETIF_F_TSO;
	else
		dev->features &= ~NETIF_F_TSO;

	pr_debug("<--eqos_set_tso\n");

	return 0;
}

/*!
 * \details This function is invoked by kernel when user
 * request to get report whether tso feature is enabled/disabled.
 *
 * \param[in] dev – pointer to net device structure.
 *
 * \return unsigned int
 *
 * \retval  +ve no. on success and -ve no. on failure.
 */

static u32 eqos_get_tso(struct net_device *dev)
{
	struct eqos_prv_data *pdata = netdev_priv(dev);

	pr_debug("-->eqos_get_tso\n");

	if (pdata->hw_feat.tso_en == 0)
		return 0;

	pr_debug("<--eqos_get_tso\n");

	return ((dev->features & NETIF_F_TSO) != 0);
}
#endif
