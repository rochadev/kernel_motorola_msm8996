/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt) "dsi-hw:" fmt
#include <linux/delay.h>

#include "dsi_ctrl_hw.h"
#include "dsi_ctrl_reg_1_4.h"
#include "dsi_hw.h"

#define MMSS_MISC_CLAMP_REG_OFF           0x0014

/* Unsupported formats default to RGB888 */
static const u8 cmd_mode_format_map[DSI_PIXEL_FORMAT_MAX] = {
	0x6, 0x7, 0x8, 0x8, 0x0, 0x3, 0x4 };
static const u8 video_mode_format_map[DSI_PIXEL_FORMAT_MAX] = {
	0x0, 0x1, 0x2, 0x3, 0x3, 0x3, 0x3 };


/**
 * dsi_setup_trigger_controls() - setup dsi trigger configurations
 * @ctrl:             Pointer to the controller host hardware.
 * @cfg:              DSI host configuration that is common to both video and
 *                    command modes.
 */
static void dsi_setup_trigger_controls(struct dsi_ctrl_hw *ctrl,
				       struct dsi_host_common_cfg *cfg)
{
	u32 reg = 0;
	const u8 trigger_map[DSI_TRIGGER_MAX] = {
		0x0, 0x2, 0x1, 0x4, 0x5, 0x6 };

	reg |= (cfg->te_mode == DSI_TE_ON_EXT_PIN) ? BIT(31) : 0;
	reg |= (trigger_map[cfg->dma_cmd_trigger] & 0x7);
	reg |= (trigger_map[cfg->mdp_cmd_trigger] & 0x7) << 4;
	DSI_W32(ctrl, DSI_TRIG_CTRL, reg);
}

/**
 * dsi_ctrl_hw_14_host_setup() - setup dsi host configuration
 * @ctrl:             Pointer to the controller host hardware.
 * @cfg:              DSI host configuration that is common to both video and
 *                    command modes.
 */
void dsi_ctrl_hw_14_host_setup(struct dsi_ctrl_hw *ctrl,
			       struct dsi_host_common_cfg *cfg)
{
	u32 reg_value = 0;

	dsi_setup_trigger_controls(ctrl, cfg);

	/* Setup clocking timing controls */
	reg_value = ((cfg->t_clk_post & 0x3F) << 8);
	reg_value |= (cfg->t_clk_pre & 0x3F);
	DSI_W32(ctrl, DSI_CLKOUT_TIMING_CTRL, reg_value);

	/* EOT packet control */
	reg_value = cfg->append_tx_eot ? 1 : 0;
	reg_value |= (cfg->ignore_rx_eot ? (1 << 4) : 0);
	DSI_W32(ctrl, DSI_EOT_PACKET_CTRL, reg_value);

	/* Turn on dsi clocks */
	DSI_W32(ctrl, DSI_CLK_CTRL, 0x23F);

	/* Setup DSI control register */
	reg_value = 0;
	reg_value |= (cfg->en_crc_check ? BIT(24) : 0);
	reg_value |= (cfg->en_ecc_check ? BIT(20) : 0);
	reg_value |= BIT(8); /* Clock lane */
	reg_value |= ((cfg->data_lanes & DSI_DATA_LANE_3) ? BIT(7) : 0);
	reg_value |= ((cfg->data_lanes & DSI_DATA_LANE_2) ? BIT(6) : 0);
	reg_value |= ((cfg->data_lanes & DSI_DATA_LANE_1) ? BIT(5) : 0);
	reg_value |= ((cfg->data_lanes & DSI_DATA_LANE_0) ? BIT(4) : 0);

	DSI_W32(ctrl, DSI_CTRL, reg_value);

	pr_debug("[DSI_%d]Host configuration complete\n", ctrl->index);
}

/**
 * phy_sw_reset() - perform a soft reset on the PHY.
 * @ctrl:        Pointer to the controller host hardware.
 */
void dsi_ctrl_hw_14_phy_sw_reset(struct dsi_ctrl_hw *ctrl)
{
	DSI_W32(ctrl, DSI_PHY_SW_RESET, 0x1);
	udelay(1000);
	DSI_W32(ctrl, DSI_PHY_SW_RESET, 0x0);
	udelay(100);

	pr_debug("[DSI_%d] phy sw reset done\n", ctrl->index);
}

/**
 * soft_reset() - perform a soft reset on DSI controller
 * @ctrl:          Pointer to the controller host hardware.
 *
 * The video, command and controller engines will be disable before the
 * reset is triggered. These engines will not be enabled after the reset
 * is complete. Caller must re-enable the engines.
 *
 * If the reset is done while MDP timing engine is turned on, the video
 * enigne should be re-enabled only during the vertical blanking time.
 */
void dsi_ctrl_hw_14_soft_reset(struct dsi_ctrl_hw *ctrl)
{
	u32 reg = 0;
	u32 reg_ctrl = 0;

	/* Clear DSI_EN, VIDEO_MODE_EN, CMD_MODE_EN */
	reg_ctrl = DSI_R32(ctrl, DSI_CTRL);
	DSI_W32(ctrl, DSI_CTRL, reg_ctrl & ~0x7);

	/* Force enable PCLK, BYTECLK, AHBM_HCLK */
	reg = DSI_R32(ctrl, DSI_CLK_CTRL);
	reg |= 0x23F;
	DSI_W32(ctrl, DSI_CLK_CTRL, reg);

	/* Trigger soft reset */
	DSI_W32(ctrl, DSI_SOFT_RESET, 0x1);
	udelay(1);
	DSI_W32(ctrl, DSI_SOFT_RESET, 0x0);

	/* Disable force clock on */
	reg &= ~(BIT(20) | BIT(11));
	DSI_W32(ctrl, DSI_CLK_CTRL, reg);

	/* Re-enable DSI controller */
	DSI_W32(ctrl, DSI_CTRL, reg_ctrl);
	pr_debug("[DSI_%d] ctrl soft reset done\n", ctrl->index);
}

/**
 * set_video_timing() - set up the timing for video frame
 * @ctrl:          Pointer to controller host hardware.
 * @mode:          Video mode information.
 *
 * Set up the video timing parameters for the DSI video mode operation.
 */
void dsi_ctrl_hw_14_set_video_timing(struct dsi_ctrl_hw *ctrl,
				     struct dsi_mode_info *mode)
{
	u32 reg = 0;
	u32 hs_start = 0;
	u32 hs_end, active_h_start, active_h_end, h_total;
	u32 vs_start = 0, vs_end = 0;
	u32 vpos_start = 0, vpos_end, active_v_start, active_v_end, v_total;

	hs_end = mode->h_sync_width;
	active_h_start = mode->h_sync_width + mode->h_back_porch;
	active_h_end = active_h_start + mode->h_active;
	h_total = (mode->h_sync_width + mode->h_back_porch + mode->h_active +
		   mode->h_front_porch) - 1;

	vpos_end = mode->v_sync_width;
	active_v_start = mode->v_sync_width + mode->v_back_porch;
	active_v_end = active_v_start + mode->v_active;
	v_total = (mode->v_sync_width + mode->v_back_porch + mode->v_active +
		   mode->v_front_porch) - 1;

	reg = ((active_h_end & 0xFFFF) << 16) | (active_h_start & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_ACTIVE_H, reg);

	reg = ((active_v_end & 0xFFFF) << 16) | (active_v_start & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_ACTIVE_V, reg);

	reg = ((v_total & 0xFFFF) << 16) | (h_total & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_TOTAL, reg);

	reg = ((hs_end & 0xFFFF) << 16) | (hs_start & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_HSYNC, reg);

	reg = ((vs_end & 0xFFFF) << 16) | (vs_start & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_VSYNC, reg);

	reg = ((vpos_end & 0xFFFF) << 16) | (vpos_start & 0xFFFF);
	DSI_W32(ctrl, DSI_VIDEO_MODE_VSYNC_VPOS, reg);

	/* TODO: HS TIMER value? */
	DSI_W32(ctrl, DSI_HS_TIMER_CTRL, 0x3FD08);
	DSI_W32(ctrl, DSI_MISR_VIDEO_CTRL, 0x10100);
	DSI_W32(ctrl, DSI_DSI_TIMING_FLUSH, 0x1);
	pr_debug("[DSI_%d] ctrl video parameters updated\n", ctrl->index);
}

/**
 * setup_cmd_stream() - set up parameters for command pixel streams
 * @ctrl:          Pointer to controller host hardware.
 * @width_in_pixels:   Width of the stream in pixels.
 * @h_stride:          Horizontal stride in bytes.
 * @height_inLines:    Number of lines in the stream.
 * @vc_id:             stream_id
 *
 * Setup parameters for command mode pixel stream size.
 */
void dsi_ctrl_hw_14_setup_cmd_stream(struct dsi_ctrl_hw *ctrl,
				     u32 width_in_pixels,
				     u32 h_stride,
				     u32 height_in_lines,
				     u32 vc_id)
{
	u32 reg = 0;

	reg = (h_stride + 1) << 16;
	reg |= (vc_id & 0x3) << 8;
	reg |= 0x39; /* packet data type */
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_STREAM0_CTRL, reg);
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_STREAM1_CTRL, reg);

	reg = (height_in_lines << 16) | width_in_pixels;
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_STREAM0_TOTAL, reg);
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_STREAM1_TOTAL, reg);
}

/**
 * video_engine_setup() - Setup dsi host controller for video mode
 * @ctrl:          Pointer to controller host hardware.
 * @common_cfg:    Common configuration parameters.
 * @cfg:           Video mode configuration.
 *
 * Set up DSI video engine with a specific configuration. Controller and
 * video engine are not enabled as part of this function.
 */
void dsi_ctrl_hw_14_video_engine_setup(struct dsi_ctrl_hw *ctrl,
				       struct dsi_host_common_cfg *common_cfg,
				       struct dsi_video_engine_cfg *cfg)
{
	u32 reg = 0;

	reg |= (cfg->last_line_interleave_en ? BIT(31) : 0);
	reg |= (cfg->pulse_mode_hsa_he ? BIT(28) : 0);
	reg |= (cfg->hfp_lp11_en ? BIT(24) : 0);
	reg |= (cfg->hbp_lp11_en ? BIT(20) : 0);
	reg |= (cfg->hsa_lp11_en ? BIT(16) : 0);
	reg |= (cfg->eof_bllp_lp11_en ? BIT(15) : 0);
	reg |= (cfg->bllp_lp11_en ? BIT(12) : 0);
	reg |= (cfg->traffic_mode & 0x3) << 8;
	reg |= (cfg->vc_id & 0x3);
	reg |= (video_mode_format_map[common_cfg->dst_format] & 0x3) << 4;
	DSI_W32(ctrl, DSI_VIDEO_MODE_CTRL, reg);

	reg = (common_cfg->swap_mode & 0x7) << 12;
	reg |= (common_cfg->bit_swap_red ? BIT(0) : 0);
	reg |= (common_cfg->bit_swap_green ? BIT(4) : 0);
	reg |= (common_cfg->bit_swap_blue ? BIT(8) : 0);
	DSI_W32(ctrl, DSI_VIDEO_MODE_DATA_CTRL, reg);
	/* Enable Timing double buffering */
	DSI_W32(ctrl, DSI_DSI_TIMING_DB_MODE, 0x1);


	pr_debug("[DSI_%d] Video engine setup done\n", ctrl->index);
}

/**
 * cmd_engine_setup() - setup dsi host controller for command mode
 * @ctrl:          Pointer to the controller host hardware.
 * @common_cfg:    Common configuration parameters.
 * @cfg:           Command mode configuration.
 *
 * Setup DSI CMD engine with a specific configuration. Controller and
 * command engine are not enabled as part of this function.
 */
void dsi_ctrl_hw_14_cmd_engine_setup(struct dsi_ctrl_hw *ctrl,
				     struct dsi_host_common_cfg *common_cfg,
				     struct dsi_cmd_engine_cfg *cfg)
{
	u32 reg = 0;

	reg = (cfg->max_cmd_packets_interleave & 0xF) << 20;
	reg |= (common_cfg->bit_swap_red ? BIT(4) : 0);
	reg |= (common_cfg->bit_swap_green ? BIT(8) : 0);
	reg |= (common_cfg->bit_swap_blue ? BIT(12) : 0);
	reg |= cmd_mode_format_map[common_cfg->dst_format];
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_CTRL, reg);

	reg = DSI_R32(ctrl, DSI_COMMAND_MODE_MDP_CTRL2);
	reg |= BIT(16);
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_CTRL2, reg);

	reg = cfg->wr_mem_start & 0xFF;
	reg |= (cfg->wr_mem_continue & 0xFF) << 8;
	reg |= (cfg->insert_dcs_command ? BIT(16) : 0);
	DSI_W32(ctrl, DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL, reg);

	pr_debug("[DSI_%d] Cmd engine setup done\n", ctrl->index);
}

/**
 * video_engine_en() - enable DSI video engine
 * @ctrl:          Pointer to controller host hardware.
 * @on:            Enable/disabel video engine.
 */
void dsi_ctrl_hw_14_video_engine_en(struct dsi_ctrl_hw *ctrl, bool on)
{
	u32 reg = 0;

	/* Set/Clear VIDEO_MODE_EN bit */
	reg = DSI_R32(ctrl, DSI_CTRL);
	if (on)
		reg |= BIT(1);
	else
		reg &= ~BIT(1);

	DSI_W32(ctrl, DSI_CTRL, reg);

	pr_debug("[DSI_%d] Video engine = %d\n", ctrl->index, on);
}

/**
 * ctrl_en() - enable DSI controller engine
 * @ctrl:          Pointer to the controller host hardware.
 * @on:            turn on/off the DSI controller engine.
 */
void dsi_ctrl_hw_14_ctrl_en(struct dsi_ctrl_hw *ctrl, bool on)
{
	u32 reg = 0;

	/* Set/Clear DSI_EN bit */
	reg = DSI_R32(ctrl, DSI_CTRL);
	if (on)
		reg |= BIT(0);
	else
		reg &= ~BIT(0);

	DSI_W32(ctrl, DSI_CTRL, reg);

	pr_debug("[DSI_%d] Controller engine = %d\n", ctrl->index, on);
}

/**
 * cmd_engine_en() - enable DSI controller command engine
 * @ctrl:          Pointer to the controller host hardware.
 * @on:            Turn on/off the DSI command engine.
 */
void dsi_ctrl_hw_14_cmd_engine_en(struct dsi_ctrl_hw *ctrl, bool on)
{
	u32 reg = 0;

	/* Set/Clear CMD_MODE_EN bit */
	reg = DSI_R32(ctrl, DSI_CTRL);
	if (on)
		reg |= BIT(2);
	else
		reg &= ~BIT(2);

	DSI_W32(ctrl, DSI_CTRL, reg);

	pr_debug("[DSI_%d] command engine = %d\n", ctrl->index, on);
}

/**
 * setup_lane_map() - setup mapping between logical and physical lanes
 * @ctrl:          Pointer to the controller host hardware.
 * @lane_map:      Structure defining the mapping between DSI logical
 *                 lanes and physical lanes.
 */
void dsi_ctrl_hw_14_setup_lane_map(struct dsi_ctrl_hw *ctrl,
			       struct dsi_lane_mapping *lane_map)
{
	u32 reg_value = 0;
	u32 lane_number = ((lane_map->physical_lane0 * 1000)+
			   (lane_map->physical_lane1 * 100) +
			   (lane_map->physical_lane2 * 10) +
			   (lane_map->physical_lane3));

	if (lane_number == 123)
		reg_value = 0;
	else if (lane_number == 3012)
		reg_value = 1;
	else if (lane_number == 2301)
		reg_value = 2;
	else if (lane_number == 1230)
		reg_value = 3;
	else if (lane_number == 321)
		reg_value = 4;
	else if (lane_number == 1032)
		reg_value = 5;
	else if (lane_number == 2103)
		reg_value = 6;
	else if (lane_number == 3210)
		reg_value = 7;

	DSI_W32(ctrl, DSI_LANE_SWAP_CTRL, reg_value);

	pr_debug("[DSI_%d] Lane swap setup complete\n", ctrl->index);
}

/**
 * kickoff_command() - transmits commands stored in memory
 * @ctrl:          Pointer to the controller host hardware.
 * @cmd:           Command information.
 * @flags:         Modifiers for command transmission.
 *
 * The controller hardware is programmed with address and size of the
 * command buffer. The transmission is kicked off if
 * DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER flag is not set. If this flag is
 * set, caller should make a separate call to trigger_command_dma() to
 * transmit the command.
 */
void dsi_ctrl_hw_14_kickoff_command(struct dsi_ctrl_hw *ctrl,
				    struct dsi_ctrl_cmd_dma_info *cmd,
				    u32 flags)
{
	u32 reg = 0;

	/*Set BROADCAST_EN and EMBEDDED_MODE */
	reg = DSI_R32(ctrl, DSI_COMMAND_MODE_DMA_CTRL);
	if (cmd->en_broadcast)
		reg |= BIT(31);
	else
		reg &= ~BIT(31);

	if (cmd->is_master)
		reg |= BIT(30);
	else
		reg &= ~BIT(30);

	if (cmd->use_lpm)
		reg |= BIT(26);
	else
		reg &= ~BIT(26);

	reg |= BIT(28);
	DSI_W32(ctrl, DSI_COMMAND_MODE_DMA_CTRL, reg);

	DSI_W32(ctrl, DSI_DMA_CMD_OFFSET, cmd->offset);
	DSI_W32(ctrl, DSI_DMA_CMD_LENGTH, (cmd->length & 0xFFFFFF));

	/* wait for writes to complete before kick off */
	wmb();

	if (!(flags & DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER))
		DSI_W32(ctrl, DSI_CMD_MODE_DMA_SW_TRIGGER, 0x1);
}

/**
 * kickoff_fifo_command() - transmits a command using FIFO in dsi
 *                          hardware.
 * @ctrl:          Pointer to the controller host hardware.
 * @cmd:           Command information.
 * @flags:         Modifiers for command transmission.
 *
 * The controller hardware FIFO is programmed with command header and
 * payload. The transmission is kicked off if
 * DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER flag is not set. If this flag is
 * set, caller should make a separate call to trigger_command_dma() to
 * transmit the command.
 */
void dsi_ctrl_hw_14_kickoff_fifo_command(struct dsi_ctrl_hw *ctrl,
					 struct dsi_ctrl_cmd_dma_fifo_info *cmd,
					 u32 flags)
{
	u32 reg = 0, i = 0;
	u32 *ptr = cmd->command;
	/*
	 * Set CMD_DMA_TPG_EN, TPG_DMA_FIFO_MODE and
	 * CMD_DMA_PATTERN_SEL = custom pattern stored in TPG DMA FIFO
	 */
	reg = (BIT(1) | BIT(2) | (0x3 << 16));
	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CTRL, reg);

	/*
	 * Program the FIFO with command buffer. Hardware requires an extra
	 * DWORD (set to zero) if the length of command buffer is odd DWORDS.
	 */
	for (i = 0; i < cmd->size; i += 4) {
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL, *ptr);
		ptr++;
	}

	if ((cmd->size / 4) & 0x1)
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CMD_DMA_INIT_VAL, 0);

	/*Set BROADCAST_EN and EMBEDDED_MODE */
	reg = DSI_R32(ctrl, DSI_COMMAND_MODE_DMA_CTRL);
	if (cmd->en_broadcast)
		reg |= BIT(31);
	else
		reg &= ~BIT(31);

	if (cmd->is_master)
		reg |= BIT(30);
	else
		reg &= ~BIT(30);

	if (cmd->use_lpm)
		reg |= BIT(26);
	else
		reg &= ~BIT(26);

	reg |= BIT(28);

	DSI_W32(ctrl, DSI_COMMAND_MODE_DMA_CTRL, reg);

	DSI_W32(ctrl, DSI_DMA_CMD_LENGTH, (cmd->size & 0xFFFFFFFF));
	/* Finish writes before command trigger */
	wmb();

	if (!(flags & DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER))
		DSI_W32(ctrl, DSI_CMD_MODE_DMA_SW_TRIGGER, 0x1);

	pr_debug("[DSI_%d]size=%d, trigger = %d\n",
		 ctrl->index, cmd->size,
		 (flags & DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER) ? false : true);
}

void dsi_ctrl_hw_14_reset_cmd_fifo(struct dsi_ctrl_hw *ctrl)
{
	/* disable cmd dma tpg */
	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CTRL, 0x0);

	DSI_W32(ctrl, DSI_TPG_DMA_FIFO_RESET, 0x1);
	udelay(1);
	DSI_W32(ctrl, DSI_TPG_DMA_FIFO_RESET, 0x0);
}

/**
 * trigger_command_dma() - trigger transmission of command buffer.
 * @ctrl:          Pointer to the controller host hardware.
 *
 * This trigger can be only used if there was a prior call to
 * kickoff_command() of kickoff_fifo_command() with
 * DSI_CTRL_HW_CMD_WAIT_FOR_TRIGGER flag.
 */
void dsi_ctrl_hw_14_trigger_command_dma(struct dsi_ctrl_hw *ctrl)
{
	DSI_W32(ctrl, DSI_CMD_MODE_DMA_SW_TRIGGER, 0x1);
	pr_debug("[DSI_%d] CMD DMA triggered\n", ctrl->index);
}

/**
 * get_cmd_read_data() - get data read from the peripheral
 * @ctrl:           Pointer to the controller host hardware.
 * @rd_buf:         Buffer where data will be read into.
 * @total_read_len: Number of bytes to read.
 *
 * return: number of bytes read.
 */
u32 dsi_ctrl_hw_14_get_cmd_read_data(struct dsi_ctrl_hw *ctrl,
				     u8 *rd_buf,
				     u32 read_offset,
				     u32 total_read_len)
{
	u32 *lp, *temp, data;
	int i, j = 0, cnt;
	u32 read_cnt;
	u32 rx_byte = 0;
	u32 repeated_bytes = 0;
	u8 reg[16];
	u32 pkt_size = 0;
	int buf_offset = read_offset;

	lp = (u32 *)rd_buf;
	temp = (u32 *)reg;
	cnt = (rx_byte + 3) >> 2;

	if (cnt > 4)
		cnt = 4;

	if (rx_byte == 4)
		read_cnt = 4;
	else
		read_cnt = pkt_size + 6;

	if (read_cnt > 16) {
		int bytes_shifted;

		bytes_shifted = read_cnt - 16;
		repeated_bytes = buf_offset - bytes_shifted;
	}

	for (i = cnt - 1; i >= 0; i--) {
		data = DSI_R32(ctrl, DSI_RDBK_DATA0 + i*4);
		*temp++ = ntohl(data);
	}

	for (i = repeated_bytes; i < 16; i++)
		rd_buf[j++] = reg[i];

	pr_debug("[DSI_%d] Read %d bytes\n", ctrl->index, j);
	return j;
}
/**
 * ulps_request() - request ulps entry for specified lanes
 * @ctrl:          Pointer to the controller host hardware.
 * @lanes:         ORed list of lanes (enum dsi_data_lanes) which need
 *                 to enter ULPS.
 *
 * Caller should check if lanes are in ULPS mode by calling
 * get_lanes_in_ulps() operation.
 */
void dsi_ctrl_hw_14_ulps_request(struct dsi_ctrl_hw *ctrl, u32 lanes)
{
	u32 reg = 0;

	if (lanes & DSI_CLOCK_LANE)
		reg = BIT(4);
	if (lanes & DSI_DATA_LANE_0)
		reg |= BIT(0);
	if (lanes & DSI_DATA_LANE_1)
		reg |= BIT(1);
	if (lanes & DSI_DATA_LANE_2)
		reg |= BIT(2);
	if (lanes & DSI_DATA_LANE_3)
		reg |= BIT(3);

	DSI_W32(ctrl, DSI_LANE_CTRL, reg);

	pr_debug("[DSI_%d] ULPS requested for lanes 0x%x\n", ctrl->index,
		 lanes);
}

/**
 * ulps_exit() - exit ULPS on specified lanes
 * @ctrl:          Pointer to the controller host hardware.
 * @lanes:         ORed list of lanes (enum dsi_data_lanes) which need
 *                 to exit ULPS.
 *
 * Caller should check if lanes are in active mode by calling
 * get_lanes_in_ulps() operation.
 */
void dsi_ctrl_hw_14_ulps_exit(struct dsi_ctrl_hw *ctrl, u32 lanes)
{
	u32 reg = 0;

	reg = DSI_R32(ctrl, DSI_LANE_CTRL);
	if (lanes & DSI_CLOCK_LANE)
		reg |= BIT(12);
	if (lanes & DSI_DATA_LANE_0)
		reg |= BIT(8);
	if (lanes & DSI_DATA_LANE_1)
		reg |= BIT(9);
	if (lanes & DSI_DATA_LANE_2)
		reg |= BIT(10);
	if (lanes & DSI_DATA_LANE_3)
		reg |= BIT(11);

	DSI_W32(ctrl, DSI_LANE_CTRL, reg);

	pr_debug("[DSI_%d] ULPS exit request for lanes=0x%x\n",
		 ctrl->index, lanes);
}

/**
 * clear_ulps_request() - clear ulps request once all lanes are active
 * @ctrl:          Pointer to controller host hardware.
 * @lanes:         ORed list of lanes (enum dsi_data_lanes).
 *
 * ULPS request should be cleared after the lanes have exited ULPS.
 */
void dsi_ctrl_hw_14_clear_ulps_request(struct dsi_ctrl_hw *ctrl, u32 lanes)
{
	u32 reg = 0;

	reg = DSI_R32(ctrl, DSI_LANE_CTRL);
	reg &= ~BIT(4); /* clock lane */
	if (lanes & DSI_DATA_LANE_0)
		reg &= ~BIT(0);
	if (lanes & DSI_DATA_LANE_1)
		reg &= ~BIT(1);
	if (lanes & DSI_DATA_LANE_2)
		reg &= ~BIT(2);
	if (lanes & DSI_DATA_LANE_3)
		reg &= ~BIT(3);

	DSI_W32(ctrl, DSI_LANE_CTRL, reg);
	/*
	 * HPG recommends separate writes for clearing ULPS_REQUEST and
	 * ULPS_EXIT.
	 */
	DSI_W32(ctrl, DSI_LANE_CTRL, 0x0);

	pr_debug("[DSI_%d] ULPS request cleared\n", ctrl->index);
}

/**
 * get_lanes_in_ulps() - returns the list of lanes in ULPS mode
 * @ctrl:          Pointer to the controller host hardware.
 *
 * Returns an ORed list of lanes (enum dsi_data_lanes) that are in ULPS
 * state. If 0 is returned, all the lanes are active.
 *
 * Return: List of lanes in ULPS state.
 */
u32 dsi_ctrl_hw_14_get_lanes_in_ulps(struct dsi_ctrl_hw *ctrl)
{
	u32 reg = 0;
	u32 lanes = 0;

	reg = DSI_R32(ctrl, DSI_LANE_STATUS);
	if (!(reg & BIT(8)))
		lanes |= DSI_DATA_LANE_0;
	if (!(reg & BIT(9)))
		lanes |= DSI_DATA_LANE_1;
	if (!(reg & BIT(10)))
		lanes |= DSI_DATA_LANE_2;
	if (!(reg & BIT(11)))
		lanes |= DSI_DATA_LANE_3;
	if (!(reg & BIT(12)))
		lanes |= DSI_CLOCK_LANE;

	pr_debug("[DSI_%d] lanes in ulps = 0x%x\n", ctrl->index, lanes);
	return lanes;
}

/**
 * clamp_enable() - enable DSI clamps to keep PHY driving a stable link
 * @ctrl:          Pointer to the controller host hardware.
 * @lanes:         ORed list of lanes which need to be clamped.
 * @enable_ulps:   TODO:??
 */
void dsi_ctrl_hw_14_clamp_enable(struct dsi_ctrl_hw *ctrl,
				 u32 lanes,
				 bool enable_ulps)
{
	u32 clamp_reg = 0;
	u32 bit_shift = 0;
	u32 reg = 0;

	if (ctrl->index == 1)
		bit_shift = 16;

	if (lanes & DSI_CLOCK_LANE) {
		clamp_reg |= BIT(9);
		if (enable_ulps)
			clamp_reg |= BIT(8);
	}

	if (lanes & DSI_DATA_LANE_0) {
		clamp_reg |= BIT(7);
		if (enable_ulps)
			clamp_reg |= BIT(6);
	}

	if (lanes & DSI_DATA_LANE_1) {
		clamp_reg |= BIT(5);
		if (enable_ulps)
			clamp_reg |= BIT(4);
	}

	if (lanes & DSI_DATA_LANE_2) {
		clamp_reg |= BIT(3);
		if (enable_ulps)
			clamp_reg |= BIT(2);
	}

	if (lanes & DSI_DATA_LANE_3) {
		clamp_reg |= BIT(1);
		if (enable_ulps)
			clamp_reg |= BIT(0);
	}

	clamp_reg |= BIT(15); /* Enable clamp */

	reg = DSI_MMSS_MISC_R32(ctrl, MMSS_MISC_CLAMP_REG_OFF);
	reg |= (clamp_reg << bit_shift);
	DSI_MMSS_MISC_W32(ctrl, MMSS_MISC_CLAMP_REG_OFF, reg);


	reg = DSI_MMSS_MISC_R32(ctrl, MMSS_MISC_CLAMP_REG_OFF);
	reg |= BIT(30);
	DSI_MMSS_MISC_W32(ctrl, MMSS_MISC_CLAMP_REG_OFF, reg);

	pr_debug("[DSI_%d] Clamps enabled for lanes=0x%x\n", ctrl->index,
		 lanes);
}

/**
 * clamp_disable() - disable DSI clamps
 * @ctrl:          Pointer to the controller host hardware.
 * @lanes:         ORed list of lanes which need to have clamps released.
 * @disable_ulps:   TODO:??
 */
void dsi_ctrl_hw_14_clamp_disable(struct dsi_ctrl_hw *ctrl,
				  u32 lanes,
				  bool disable_ulps)
{
	u32 clamp_reg = 0;
	u32 bit_shift = 0;
	u32 reg = 0;

	if (ctrl->index == 1)
		bit_shift = 16;

	if (lanes & DSI_CLOCK_LANE) {
		clamp_reg |= BIT(9);
		if (disable_ulps)
			clamp_reg |= BIT(8);
	}

	if (lanes & DSI_DATA_LANE_0) {
		clamp_reg |= BIT(7);
		if (disable_ulps)
			clamp_reg |= BIT(6);
	}

	if (lanes & DSI_DATA_LANE_1) {
		clamp_reg |= BIT(5);
		if (disable_ulps)
			clamp_reg |= BIT(4);
	}

	if (lanes & DSI_DATA_LANE_2) {
		clamp_reg |= BIT(3);
		if (disable_ulps)
			clamp_reg |= BIT(2);
	}

	if (lanes & DSI_DATA_LANE_3) {
		clamp_reg |= BIT(1);
		if (disable_ulps)
			clamp_reg |= BIT(0);
	}

	clamp_reg |= BIT(15); /* Enable clamp */
	clamp_reg <<= bit_shift;

	/* Disable PHY reset skip */
	reg = DSI_MMSS_MISC_R32(ctrl, MMSS_MISC_CLAMP_REG_OFF);
	reg &= ~BIT(30);
	DSI_MMSS_MISC_W32(ctrl, MMSS_MISC_CLAMP_REG_OFF, reg);

	reg = DSI_MMSS_MISC_R32(ctrl, MMSS_MISC_CLAMP_REG_OFF);
	reg &= ~(clamp_reg);
	DSI_MMSS_MISC_W32(ctrl, MMSS_MISC_CLAMP_REG_OFF, reg);

	pr_debug("[DSI_%d] Disable clamps for lanes=%d\n", ctrl->index, lanes);
}

/**
 * get_interrupt_status() - returns the interrupt status
 * @ctrl:          Pointer to the controller host hardware.
 *
 * Returns the ORed list of interrupts(enum dsi_status_int_type) that
 * are active. This list does not include any error interrupts. Caller
 * should call get_error_status for error interrupts.
 *
 * Return: List of active interrupts.
 */
u32 dsi_ctrl_hw_14_get_interrupt_status(struct dsi_ctrl_hw *ctrl)
{
	u32 reg = 0;
	u32 ints = 0;

	reg = DSI_R32(ctrl, DSI_INT_CTRL);

	if (reg & BIT(0))
		ints |= DSI_CMD_MODE_DMA_DONE;
	if (reg & BIT(8))
		ints |= DSI_CMD_FRAME_DONE;
	if (reg & BIT(10))
		ints |= DSI_CMD_STREAM0_FRAME_DONE;
	if (reg & BIT(12))
		ints |= DSI_CMD_STREAM1_FRAME_DONE;
	if (reg & BIT(14))
		ints |= DSI_CMD_STREAM2_FRAME_DONE;
	if (reg & BIT(16))
		ints |= DSI_VIDEO_MODE_FRAME_DONE;
	if (reg & BIT(20))
		ints |= DSI_BTA_DONE;
	if (reg & BIT(28))
		ints |= DSI_DYN_REFRESH_DONE;
	if (reg & BIT(30))
		ints |= DSI_DESKEW_DONE;

	pr_debug("[DSI_%d] Interrupt status = 0x%x, INT_CTRL=0x%x\n",
		 ctrl->index, ints, reg);
	return ints;
}

/**
 * clear_interrupt_status() - clears the specified interrupts
 * @ctrl:          Pointer to the controller host hardware.
 * @ints:          List of interrupts to be cleared.
 */
void dsi_ctrl_hw_14_clear_interrupt_status(struct dsi_ctrl_hw *ctrl, u32 ints)
{
	u32 reg = 0;

	if (ints & DSI_CMD_MODE_DMA_DONE)
		reg |= BIT(0);
	if (ints & DSI_CMD_FRAME_DONE)
		reg |= BIT(8);
	if (ints & DSI_CMD_STREAM0_FRAME_DONE)
		reg |= BIT(10);
	if (ints & DSI_CMD_STREAM1_FRAME_DONE)
		reg |= BIT(12);
	if (ints & DSI_CMD_STREAM2_FRAME_DONE)
		reg |= BIT(14);
	if (ints & DSI_VIDEO_MODE_FRAME_DONE)
		reg |= BIT(16);
	if (ints & DSI_BTA_DONE)
		reg |= BIT(20);
	if (ints & DSI_DYN_REFRESH_DONE)
		reg |= BIT(28);
	if (ints & DSI_DESKEW_DONE)
		reg |= BIT(30);

	DSI_W32(ctrl, DSI_INT_CTRL, reg);

	pr_debug("[DSI_%d] Clear interrupts, ints = 0x%x, INT_CTRL=0x%x\n",
		 ctrl->index, ints, reg);
}

/**
 * enable_status_interrupts() - enable the specified interrupts
 * @ctrl:          Pointer to the controller host hardware.
 * @ints:          List of interrupts to be enabled.
 *
 * Enables the specified interrupts. This list will override the
 * previous interrupts enabled through this function. Caller has to
 * maintain the state of the interrupts enabled. To disable all
 * interrupts, set ints to 0.
 */
void dsi_ctrl_hw_14_enable_status_interrupts(struct dsi_ctrl_hw *ctrl, u32 ints)
{
	u32 reg = 0;

	/* Do not change value of DSI_ERROR_MASK bit */
	reg |= (DSI_R32(ctrl, DSI_INT_CTRL) & BIT(25));
	if (ints & DSI_CMD_MODE_DMA_DONE)
		reg |= BIT(1);
	if (ints & DSI_CMD_FRAME_DONE)
		reg |= BIT(9);
	if (ints & DSI_CMD_STREAM0_FRAME_DONE)
		reg |= BIT(11);
	if (ints & DSI_CMD_STREAM1_FRAME_DONE)
		reg |= BIT(13);
	if (ints & DSI_CMD_STREAM2_FRAME_DONE)
		reg |= BIT(15);
	if (ints & DSI_VIDEO_MODE_FRAME_DONE)
		reg |= BIT(17);
	if (ints & DSI_BTA_DONE)
		reg |= BIT(21);
	if (ints & DSI_DYN_REFRESH_DONE)
		reg |= BIT(29);
	if (ints & DSI_DESKEW_DONE)
		reg |= BIT(31);

	DSI_W32(ctrl, DSI_INT_CTRL, reg);

	pr_debug("[DSI_%d] Enable interrupts 0x%x, INT_CTRL=0x%x\n",
		 ctrl->index, ints, reg);
}

/**
 * get_error_status() - returns the error status
 * @ctrl:          Pointer to the controller host hardware.
 *
 * Returns the ORed list of errors(enum dsi_error_int_type) that are
 * active. This list does not include any status interrupts. Caller
 * should call get_interrupt_status for status interrupts.
 *
 * Return: List of active error interrupts.
 */
u64 dsi_ctrl_hw_14_get_error_status(struct dsi_ctrl_hw *ctrl)
{
	u32 dln0_phy_err;
	u32 fifo_status;
	u32 ack_error;
	u32 timeout_errors;
	u32 clk_error;
	u32 dsi_status;
	u64 errors = 0;

	dln0_phy_err = DSI_R32(ctrl, DSI_DLN0_PHY_ERR);
	if (dln0_phy_err & BIT(0))
		errors |= DSI_DLN0_ESC_ENTRY_ERR;
	if (dln0_phy_err & BIT(4))
		errors |= DSI_DLN0_ESC_SYNC_ERR;
	if (dln0_phy_err & BIT(8))
		errors |= DSI_DLN0_LP_CONTROL_ERR;
	if (dln0_phy_err & BIT(12))
		errors |= DSI_DLN0_LP0_CONTENTION;
	if (dln0_phy_err & BIT(16))
		errors |= DSI_DLN0_LP1_CONTENTION;

	fifo_status = DSI_R32(ctrl, DSI_FIFO_STATUS);
	if (fifo_status & BIT(7))
		errors |= DSI_CMD_MDP_FIFO_UNDERFLOW;
	if (fifo_status & BIT(10))
		errors |= DSI_CMD_DMA_FIFO_UNDERFLOW;
	if (fifo_status & BIT(18))
		errors |= DSI_DLN0_HS_FIFO_OVERFLOW;
	if (fifo_status & BIT(19))
		errors |= DSI_DLN0_HS_FIFO_UNDERFLOW;
	if (fifo_status & BIT(22))
		errors |= DSI_DLN1_HS_FIFO_OVERFLOW;
	if (fifo_status & BIT(23))
		errors |= DSI_DLN1_HS_FIFO_UNDERFLOW;
	if (fifo_status & BIT(26))
		errors |= DSI_DLN2_HS_FIFO_OVERFLOW;
	if (fifo_status & BIT(27))
		errors |= DSI_DLN2_HS_FIFO_UNDERFLOW;
	if (fifo_status & BIT(30))
		errors |= DSI_DLN3_HS_FIFO_OVERFLOW;
	if (fifo_status & BIT(31))
		errors |= DSI_DLN3_HS_FIFO_UNDERFLOW;

	ack_error = DSI_R32(ctrl, DSI_ACK_ERR_STATUS);
	if (ack_error & BIT(16))
		errors |= DSI_RDBK_SINGLE_ECC_ERR;
	if (ack_error & BIT(17))
		errors |= DSI_RDBK_MULTI_ECC_ERR;
	if (ack_error & BIT(20))
		errors |= DSI_RDBK_CRC_ERR;
	if (ack_error & BIT(23))
		errors |= DSI_RDBK_INCOMPLETE_PKT;
	if (ack_error & BIT(24))
		errors |= DSI_PERIPH_ERROR_PKT;

	timeout_errors = DSI_R32(ctrl, DSI_TIMEOUT_STATUS);
	if (timeout_errors & BIT(0))
		errors |= DSI_HS_TX_TIMEOUT;
	if (timeout_errors & BIT(4))
		errors |= DSI_LP_RX_TIMEOUT;
	if (timeout_errors & BIT(8))
		errors |= DSI_BTA_TIMEOUT;

	clk_error = DSI_R32(ctrl, DSI_CLK_STATUS);
	if (clk_error & BIT(16))
		errors |= DSI_PLL_UNLOCK;

	dsi_status = DSI_R32(ctrl, DSI_STATUS);
	if (dsi_status & BIT(31))
		errors |= DSI_INTERLEAVE_OP_CONTENTION;

	pr_debug("[DSI_%d] Error status = 0x%llx, phy=0x%x, fifo=0x%x",
		 ctrl->index, errors, dln0_phy_err, fifo_status);
	pr_debug("[DSI_%d] ack=0x%x, timeout=0x%x, clk=0x%x, dsi=0x%x\n",
		 ctrl->index, ack_error, timeout_errors, clk_error, dsi_status);
	return errors;
}

/**
 * clear_error_status() - clears the specified errors
 * @ctrl:          Pointer to the controller host hardware.
 * @errors:          List of errors to be cleared.
 */
void dsi_ctrl_hw_14_clear_error_status(struct dsi_ctrl_hw *ctrl, u64 errors)
{
	u32 dln0_phy_err = 0;
	u32 fifo_status = 0;
	u32 ack_error = 0;
	u32 timeout_error = 0;
	u32 clk_error = 0;
	u32 dsi_status = 0;
	u32 int_ctrl = 0;

	if (errors & DSI_RDBK_SINGLE_ECC_ERR)
		ack_error |= BIT(16);
	if (errors & DSI_RDBK_MULTI_ECC_ERR)
		ack_error |= BIT(17);
	if (errors & DSI_RDBK_CRC_ERR)
		ack_error |= BIT(20);
	if (errors & DSI_RDBK_INCOMPLETE_PKT)
		ack_error |= BIT(23);
	if (errors & DSI_PERIPH_ERROR_PKT)
		ack_error |= BIT(24);

	if (errors & DSI_LP_RX_TIMEOUT)
		timeout_error |= BIT(4);
	if (errors & DSI_HS_TX_TIMEOUT)
		timeout_error |= BIT(0);
	if (errors & DSI_BTA_TIMEOUT)
		timeout_error |= BIT(8);

	if (errors & DSI_PLL_UNLOCK)
		clk_error |= BIT(16);

	if (errors & DSI_DLN0_LP0_CONTENTION)
		dln0_phy_err |= BIT(12);
	if (errors & DSI_DLN0_LP1_CONTENTION)
		dln0_phy_err |= BIT(16);
	if (errors & DSI_DLN0_ESC_ENTRY_ERR)
		dln0_phy_err |= BIT(0);
	if (errors & DSI_DLN0_ESC_SYNC_ERR)
		dln0_phy_err |= BIT(4);
	if (errors & DSI_DLN0_LP_CONTROL_ERR)
		dln0_phy_err |= BIT(8);

	if (errors & DSI_CMD_DMA_FIFO_UNDERFLOW)
		fifo_status |= BIT(10);
	if (errors & DSI_CMD_MDP_FIFO_UNDERFLOW)
		fifo_status |= BIT(7);
	if (errors & DSI_DLN0_HS_FIFO_OVERFLOW)
		fifo_status |= BIT(18);
	if (errors & DSI_DLN1_HS_FIFO_OVERFLOW)
		fifo_status |= BIT(22);
	if (errors & DSI_DLN2_HS_FIFO_OVERFLOW)
		fifo_status |= BIT(26);
	if (errors & DSI_DLN3_HS_FIFO_OVERFLOW)
		fifo_status |= BIT(30);
	if (errors & DSI_DLN0_HS_FIFO_UNDERFLOW)
		fifo_status |= BIT(19);
	if (errors & DSI_DLN1_HS_FIFO_UNDERFLOW)
		fifo_status |= BIT(23);
	if (errors & DSI_DLN2_HS_FIFO_UNDERFLOW)
		fifo_status |= BIT(27);
	if (errors & DSI_DLN3_HS_FIFO_UNDERFLOW)
		fifo_status |= BIT(31);

	if (errors & DSI_INTERLEAVE_OP_CONTENTION)
		dsi_status |= BIT(31);

	DSI_W32(ctrl, DSI_DLN0_PHY_ERR, dln0_phy_err);
	DSI_W32(ctrl, DSI_FIFO_STATUS, fifo_status);
	DSI_W32(ctrl, DSI_ACK_ERR_STATUS, ack_error);
	DSI_W32(ctrl, DSI_TIMEOUT_STATUS, timeout_error);
	DSI_W32(ctrl, DSI_CLK_STATUS, clk_error);
	DSI_W32(ctrl, DSI_STATUS, dsi_status);

	int_ctrl = DSI_R32(ctrl, DSI_INT_CTRL);
	int_ctrl |= BIT(24);
	DSI_W32(ctrl, DSI_INT_CTRL, int_ctrl);
	pr_debug("[DSI_%d] clear errors = 0x%llx, phy=0x%x, fifo=0x%x",
		 ctrl->index, errors, dln0_phy_err, fifo_status);
	pr_debug("[DSI_%d] ack=0x%x, timeout=0x%x, clk=0x%x, dsi=0x%x\n",
		 ctrl->index, ack_error, timeout_error, clk_error, dsi_status);
}

/**
 * enable_error_interrupts() - enable the specified interrupts
 * @ctrl:          Pointer to the controller host hardware.
 * @errors:        List of errors to be enabled.
 *
 * Enables the specified interrupts. This list will override the
 * previous interrupts enabled through this function. Caller has to
 * maintain the state of the interrupts enabled. To disable all
 * interrupts, set errors to 0.
 */
void dsi_ctrl_hw_14_enable_error_interrupts(struct dsi_ctrl_hw *ctrl,
					    u64 errors)
{
	u32 int_ctrl = 0;
	u32 int_mask0 = 0x7FFF3BFF;

	int_ctrl = DSI_R32(ctrl, DSI_INT_CTRL);
	if (errors)
		int_ctrl |= BIT(25);
	else
		int_ctrl &= ~BIT(25);

	if (errors & DSI_RDBK_SINGLE_ECC_ERR)
		int_mask0 &= ~BIT(0);
	if (errors & DSI_RDBK_MULTI_ECC_ERR)
		int_mask0 &= ~BIT(1);
	if (errors & DSI_RDBK_CRC_ERR)
		int_mask0 &= ~BIT(2);
	if (errors & DSI_RDBK_INCOMPLETE_PKT)
		int_mask0 &= ~BIT(3);
	if (errors & DSI_PERIPH_ERROR_PKT)
		int_mask0 &= ~BIT(4);

	if (errors & DSI_LP_RX_TIMEOUT)
		int_mask0 &= ~BIT(5);
	if (errors & DSI_HS_TX_TIMEOUT)
		int_mask0 &= ~BIT(6);
	if (errors & DSI_BTA_TIMEOUT)
		int_mask0 &= ~BIT(7);

	if (errors & DSI_PLL_UNLOCK)
		int_mask0 &= ~BIT(28);

	if (errors & DSI_DLN0_LP0_CONTENTION)
		int_mask0 &= ~BIT(24);
	if (errors & DSI_DLN0_LP1_CONTENTION)
		int_mask0 &= ~BIT(25);
	if (errors & DSI_DLN0_ESC_ENTRY_ERR)
		int_mask0 &= ~BIT(21);
	if (errors & DSI_DLN0_ESC_SYNC_ERR)
		int_mask0 &= ~BIT(22);
	if (errors & DSI_DLN0_LP_CONTROL_ERR)
		int_mask0 &= ~BIT(23);

	if (errors & DSI_CMD_DMA_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(9);
	if (errors & DSI_CMD_MDP_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(11);
	if (errors & DSI_DLN0_HS_FIFO_OVERFLOW)
		int_mask0 &= ~BIT(16);
	if (errors & DSI_DLN1_HS_FIFO_OVERFLOW)
		int_mask0 &= ~BIT(17);
	if (errors & DSI_DLN2_HS_FIFO_OVERFLOW)
		int_mask0 &= ~BIT(18);
	if (errors & DSI_DLN3_HS_FIFO_OVERFLOW)
		int_mask0 &= ~BIT(19);
	if (errors & DSI_DLN0_HS_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(26);
	if (errors & DSI_DLN1_HS_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(27);
	if (errors & DSI_DLN2_HS_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(29);
	if (errors & DSI_DLN3_HS_FIFO_UNDERFLOW)
		int_mask0 &= ~BIT(30);

	if (errors & DSI_INTERLEAVE_OP_CONTENTION)
		int_mask0 &= ~BIT(8);

	DSI_W32(ctrl, DSI_INT_CTRL, int_ctrl);
	DSI_W32(ctrl, DSI_ERR_INT_MASK0, int_mask0);

	pr_debug("[DSI_%d] enable errors = 0x%llx, int_mask0=0x%x\n",
		 ctrl->index, errors, int_mask0);
}

/**
 * video_test_pattern_setup() - setup test pattern engine for video mode
 * @ctrl:          Pointer to the controller host hardware.
 * @type:          Type of test pattern.
 * @init_val:      Initial value to use for generating test pattern.
 */
void dsi_ctrl_hw_14_video_test_pattern_setup(struct dsi_ctrl_hw *ctrl,
					     enum dsi_test_pattern type,
					     u32 init_val)
{
	u32 reg = 0;

	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_VIDEO_INIT_VAL, init_val);

	switch (type) {
	case DSI_TEST_PATTERN_FIXED:
		reg |= (0x2 << 4);
		break;
	case DSI_TEST_PATTERN_INC:
		reg |= (0x1 << 4);
		break;
	case DSI_TEST_PATTERN_POLY:
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_VIDEO_POLY, 0xF0F0F);
		break;
	default:
		break;
	}

	DSI_W32(ctrl, DSI_TPG_MAIN_CONTROL, 0x100);
	DSI_W32(ctrl, DSI_TPG_VIDEO_CONFIG, 0x5);
	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CTRL, reg);

	pr_debug("[DSI_%d] Video test pattern setup done\n", ctrl->index);
}

/**
 * cmd_test_pattern_setup() - setup test patttern engine for cmd mode
 * @ctrl:          Pointer to the controller host hardware.
 * @type:          Type of test pattern.
 * @init_val:      Initial value to use for generating test pattern.
 * @stream_id:     Stream Id on which packets are generated.
 */
void dsi_ctrl_hw_14_cmd_test_pattern_setup(struct dsi_ctrl_hw *ctrl,
					   enum dsi_test_pattern type,
					   u32 init_val,
					   u32 stream_id)
{
	u32 reg = 0;
	u32 init_offset;
	u32 poly_offset;
	u32 pattern_sel_shift;

	switch (stream_id) {
	case 0:
		init_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_INIT_VAL0;
		poly_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_STREAM0_POLY;
		pattern_sel_shift = 8;
		break;
	case 1:
		init_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_INIT_VAL1;
		poly_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_STREAM1_POLY;
		pattern_sel_shift = 12;
		break;
	case 2:
		init_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_INIT_VAL2;
		poly_offset = DSI_TEST_PATTERN_GEN_CMD_MDP_STREAM2_POLY;
		pattern_sel_shift = 20;
		break;
	default:
		return;
	}

	DSI_W32(ctrl, init_offset, init_val);

	switch (type) {
	case DSI_TEST_PATTERN_FIXED:
		reg |= (0x2 << pattern_sel_shift);
		break;
	case DSI_TEST_PATTERN_INC:
		reg |= (0x1 << pattern_sel_shift);
		break;
	case DSI_TEST_PATTERN_POLY:
		DSI_W32(ctrl, poly_offset, 0xF0F0F);
		break;
	default:
		break;
	}

	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CTRL, reg);
	pr_debug("[DSI_%d] Cmd test pattern setup done\n", ctrl->index);
}

/**
 * test_pattern_enable() - enable test pattern engine
 * @ctrl:          Pointer to the controller host hardware.
 * @enable:        Enable/Disable test pattern engine.
 */
void dsi_ctrl_hw_14_test_pattern_enable(struct dsi_ctrl_hw *ctrl,
					bool enable)
{
	u32 reg = DSI_R32(ctrl, DSI_TEST_PATTERN_GEN_CTRL);

	if (enable)
		reg |= BIT(0);
	else
		reg &= ~BIT(0);

	DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CTRL, reg);

	pr_debug("[DSI_%d] Test pattern enable=%d\n", ctrl->index, enable);
}

/**
 * trigger_cmd_test_pattern() - trigger a command mode frame update with
 *                              test pattern
 * @ctrl:          Pointer to the controller host hardware.
 * @stream_id:     Stream on which frame update is sent.
 */
void dsi_ctrl_hw_14_trigger_cmd_test_pattern(struct dsi_ctrl_hw *ctrl,
					     u32 stream_id)
{
	switch (stream_id) {
	case 0:
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CMD_STREAM0_TRIGGER, 0x1);
		break;
	case 1:
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CMD_STREAM1_TRIGGER, 0x1);
		break;
	case 2:
		DSI_W32(ctrl, DSI_TEST_PATTERN_GEN_CMD_STREAM2_TRIGGER, 0x1);
		break;
	default:
		break;
	}

	pr_debug("[DSI_%d] Cmd Test pattern trigger\n", ctrl->index);
}
