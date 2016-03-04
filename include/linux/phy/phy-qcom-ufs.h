/*
 * Copyright (c) 2013-2016, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef PHY_QCOM_UFS_H_
#define PHY_QCOM_UFS_H_

#include "phy.h"

int ufs_qcom_phy_enable_ref_clk(struct phy *phy);
void ufs_qcom_phy_disable_ref_clk(struct phy *phy);
int ufs_qcom_phy_enable_iface_clk(struct phy *phy);
void ufs_qcom_phy_disable_iface_clk(struct phy *phy);
int ufs_qcom_phy_start_serdes(struct phy *phy);
int ufs_qcom_phy_set_tx_lane_enable(struct phy *phy, u32 tx_lanes);
int ufs_qcom_phy_ctrl_rx_linecfg(struct phy *generic_phy, bool ctrl);
int ufs_qcom_phy_calibrate_phy(struct phy *phy, bool is_rate_B);
int ufs_qcom_phy_is_pcs_ready(struct phy *phy);
void ufs_qcom_phy_save_controller_version(struct phy *phy,
			u8 major, u16 minor, u16 step);
const char *ufs_qcom_phy_name(struct phy *phy);
#endif
