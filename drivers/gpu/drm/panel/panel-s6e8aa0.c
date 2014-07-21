/*
 * MIPI-DSI based s6e8aa0 AMOLED LCD 5.3 inch panel driver.
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd
 *
 * Inki Dae, <inki.dae@samsung.com>
 * Donghwa Lee, <dh09.lee@samsung.com>
 * Joongmock Shin <jmock.shin@samsung.com>
 * Eunchul Kim <chulspro.kim@samsung.com>
 * Tomasz Figa <t.figa@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#define LDI_MTP_LENGTH			24
#define GAMMA_LEVEL_NUM			25
#define GAMMA_TABLE_LEN			26

#define PANELCTL_SS_MASK		(1 << 5)
#define PANELCTL_SS_1_800		(0 << 5)
#define PANELCTL_SS_800_1		(1 << 5)
#define PANELCTL_GTCON_MASK		(7 << 2)
#define PANELCTL_GTCON_110		(6 << 2)
#define PANELCTL_GTCON_111		(7 << 2)

#define PANELCTL_CLK1_CON_MASK		(7 << 3)
#define PANELCTL_CLK1_000		(0 << 3)
#define PANELCTL_CLK1_001		(1 << 3)
#define PANELCTL_CLK2_CON_MASK		(7 << 0)
#define PANELCTL_CLK2_000		(0 << 0)
#define PANELCTL_CLK2_001		(1 << 0)

#define PANELCTL_INT1_CON_MASK		(7 << 3)
#define PANELCTL_INT1_000		(0 << 3)
#define PANELCTL_INT1_001		(1 << 3)
#define PANELCTL_INT2_CON_MASK		(7 << 0)
#define PANELCTL_INT2_000		(0 << 0)
#define PANELCTL_INT2_001		(1 << 0)

#define PANELCTL_BICTL_CON_MASK		(7 << 3)
#define PANELCTL_BICTL_000		(0 << 3)
#define PANELCTL_BICTL_001		(1 << 3)
#define PANELCTL_BICTLB_CON_MASK	(7 << 0)
#define PANELCTL_BICTLB_000		(0 << 0)
#define PANELCTL_BICTLB_001		(1 << 0)

#define PANELCTL_EM_CLK1_CON_MASK	(7 << 3)
#define PANELCTL_EM_CLK1_110		(6 << 3)
#define PANELCTL_EM_CLK1_111		(7 << 3)
#define PANELCTL_EM_CLK1B_CON_MASK	(7 << 0)
#define PANELCTL_EM_CLK1B_110		(6 << 0)
#define PANELCTL_EM_CLK1B_111		(7 << 0)

#define PANELCTL_EM_CLK2_CON_MASK	(7 << 3)
#define PANELCTL_EM_CLK2_110		(6 << 3)
#define PANELCTL_EM_CLK2_111		(7 << 3)
#define PANELCTL_EM_CLK2B_CON_MASK	(7 << 0)
#define PANELCTL_EM_CLK2B_110		(6 << 0)
#define PANELCTL_EM_CLK2B_111		(7 << 0)

#define PANELCTL_EM_INT1_CON_MASK	(7 << 3)
#define PANELCTL_EM_INT1_000		(0 << 3)
#define PANELCTL_EM_INT1_001		(1 << 3)
#define PANELCTL_EM_INT2_CON_MASK	(7 << 0)
#define PANELCTL_EM_INT2_000		(0 << 0)
#define PANELCTL_EM_INT2_001		(1 << 0)

#define AID_DISABLE			(0x4)
#define AID_1				(0x5)
#define AID_2				(0x6)
#define AID_3				(0x7)

typedef u8 s6e8aa0_gamma_table[GAMMA_TABLE_LEN];

struct s6e8aa0_variant {
	u8 version;
	const s6e8aa0_gamma_table *gamma_tables;
};

struct s6e8aa0 {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
	u32 power_on_delay;
	u32 reset_delay;
	u32 init_delay;
	bool flip_horizontal;
	bool flip_vertical;
	struct videomode vm;
	u32 width_mm;
	u32 height_mm;

	u8 version;
	u8 id;
	const struct s6e8aa0_variant *variant;
	int brightness;

	/* This field is tested by functions directly accessing DSI bus before
	 * transfer, transfer is skipped if it is set. In case of transfer
	 * failure or unexpected response the field is set to error value.
	 * Such construct allows to eliminate many checks in higher level
	 * functions.
	 */
	int error;
};

#define panel_to_s6e8aa0(p) container_of(p, struct s6e8aa0, panel)

static int s6e8aa0_clear_error(struct s6e8aa0 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static void s6e8aa0_dcs_write(struct s6e8aa0 *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return;

	ret = mipi_dsi_dcs_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing dcs seq: %*ph\n", ret, len,
			data);
		ctx->error = ret;
	}
}

static int s6e8aa0_dcs_read(struct s6e8aa0 *ctx, u8 cmd, void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (ctx->error < 0)
		return ctx->error;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}

#define s6e8aa0_dcs_write_seq(ctx, seq...) \
({\
	const u8 d[] = { seq };\
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, "DCS sequence too big for stack");\
	s6e8aa0_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

#define s6e8aa0_dcs_write_seq_static(ctx, seq...) \
({\
	static const u8 d[] = { seq };\
	s6e8aa0_dcs_write(ctx, d, ARRAY_SIZE(d));\
})

static void s6e8aa0_apply_level_1_key(struct s6e8aa0 *ctx)
{
	s6e8aa0_dcs_write_seq_static(ctx, 0xf0, 0x5a, 0x5a);
}

static void s6e8aa0_panel_cond_set_v142(struct s6e8aa0 *ctx)
{
	static const u8 aids[] = {
		0x04, 0x04, 0x04, 0x04, 0x04, 0x60, 0x80, 0xA0
	};
	u8 aid = aids[ctx->id >> 5];
	u8 cfg = 0x3d;
	u8 clk_con = 0xc8;
	u8 int_con = 0x08;
	u8 bictl_con = 0x48;
	u8 em_clk1_con = 0xff;
	u8 em_clk2_con = 0xff;
	u8 em_int_con = 0xc8;

	if (ctx->flip_vertical) {
		/* GTCON */
		cfg &= ~(PANELCTL_GTCON_MASK);
		cfg |= (PANELCTL_GTCON_110);
	}

	if (ctx->flip_horizontal) {
		/* SS */
		cfg &= ~(PANELCTL_SS_MASK);
		cfg |= (PANELCTL_SS_1_800);
	}

	if (ctx->flip_horizontal || ctx->flip_vertical) {
		/* CLK1,2_CON */
		clk_con &= ~(PANELCTL_CLK1_CON_MASK |
			PANELCTL_CLK2_CON_MASK);
		clk_con |= (PANELCTL_CLK1_000 | PANELCTL_CLK2_001);

		/* INT1,2_CON */
		int_con &= ~(PANELCTL_INT1_CON_MASK |
			PANELCTL_INT2_CON_MASK);
		int_con |= (PANELCTL_INT1_000 | PANELCTL_INT2_001);

		/* BICTL,B_CON */
		bictl_con &= ~(PANELCTL_BICTL_CON_MASK |
			PANELCTL_BICTLB_CON_MASK);
		bictl_con |= (PANELCTL_BICTL_000 |
			PANELCTL_BICTLB_001);

		/* EM_CLK1,1B_CON */
		em_clk1_con &= ~(PANELCTL_EM_CLK1_CON_MASK |
			PANELCTL_EM_CLK1B_CON_MASK);
		em_clk1_con |= (PANELCTL_EM_CLK1_110 |
			PANELCTL_EM_CLK1B_110);

		/* EM_CLK2,2B_CON */
		em_clk2_con &= ~(PANELCTL_EM_CLK2_CON_MASK |
			PANELCTL_EM_CLK2B_CON_MASK);
		em_clk2_con |= (PANELCTL_EM_CLK2_110 |
			PANELCTL_EM_CLK2B_110);

		/* EM_INT1,2_CON */
		em_int_con &= ~(PANELCTL_EM_INT1_CON_MASK |
			PANELCTL_EM_INT2_CON_MASK);
		em_int_con |= (PANELCTL_EM_INT1_000 |
			PANELCTL_EM_INT2_001);
	}

	s6e8aa0_dcs_write_seq(ctx,
		0xf8, cfg, 0x35, 0x00, 0x00, 0x00, 0x93, 0x00,
		0x3c, 0x78, 0x08, 0x27, 0x7d, 0x3f, 0x00, 0x00,
		0x00, 0x20, aid, 0x08, 0x6e, 0x00, 0x00, 0x00,
		0x02, 0x07, 0x07, 0x23, 0x23, 0xc0, clk_con, int_con,
		bictl_con, 0xc1, 0x00, 0xc1, em_clk1_con, em_clk2_con,
		em_int_con);
}

static void s6e8aa0_panel_cond_set(struct s6e8aa0 *ctx)
{
	if (ctx->version < 142)
		s6e8aa0_dcs_write_seq_static(ctx,
			0xf8, 0x19, 0x35, 0x00, 0x00, 0x00, 0x94, 0x00,
			0x3c, 0x78, 0x10, 0x27, 0x08, 0x6e, 0x00, 0x00,
			0x00, 0x00, 0x04, 0x08, 0x6e, 0x00, 0x00, 0x00,
			0x00, 0x07, 0x07, 0x23, 0x6e, 0xc0, 0xc1, 0x01,
			0x81, 0xc1, 0x00, 0xc3, 0xf6, 0xf6, 0xc1
		);
	else
		s6e8aa0_panel_cond_set_v142(ctx);
}

static void s6e8aa0_display_condition_set(struct s6e8aa0 *ctx)
{
	s6e8aa0_dcs_write_seq_static(ctx, 0xf2, 0x80, 0x03, 0x0d);
}

static void s6e8aa0_etc_source_control(struct s6e8aa0 *ctx)
{
	s6e8aa0_dcs_write_seq_static(ctx, 0xf6, 0x00, 0x02, 0x00);
}

static void s6e8aa0_etc_pentile_control(struct s6e8aa0 *ctx)
{
	static const u8 pent32[] = {
		0xb6, 0x0c, 0x02, 0x03, 0x32, 0xc0, 0x44, 0x44, 0xc0, 0x00
	};

	static const u8 pent142[] = {
		0xb6, 0x0c, 0x02, 0x03, 0x32, 0xff, 0x44, 0x44, 0xc0, 0x00
	};

	if (ctx->version < 142)
		s6e8aa0_dcs_write(ctx, pent32, ARRAY_SIZE(pent32));
	else
		s6e8aa0_dcs_write(ctx, pent142, ARRAY_SIZE(pent142));
}

static void s6e8aa0_etc_power_control(struct s6e8aa0 *ctx)
{
	static const u8 pwr142[] = {
		0xf4, 0xcf, 0x0a, 0x12, 0x10, 0x1e, 0x33, 0x02
	};

	static const u8 pwr32[] = {
		0xf4, 0xcf, 0x0a, 0x15, 0x10, 0x19, 0x33, 0x02
	};

	if (ctx->version < 142)
		s6e8aa0_dcs_write(ctx, pwr32, ARRAY_SIZE(pwr32));
	else
		s6e8aa0_dcs_write(ctx, pwr142, ARRAY_SIZE(pwr142));
}

static void s6e8aa0_etc_elvss_control(struct s6e8aa0 *ctx)
{
	u8 id = ctx->id ? 0 : 0x95;

	s6e8aa0_dcs_write_seq(ctx, 0xb1, 0x04, id);
}

static void s6e8aa0_elvss_nvm_set_v142(struct s6e8aa0 *ctx)
{
	u8 br;

	switch (ctx->brightness) {
	case 0 ... 6: /* 30cd ~ 100cd */
		br = 0xdf;
		break;
	case 7 ... 11: /* 120cd ~ 150cd */
		br = 0xdd;
		break;
	case 12 ... 15: /* 180cd ~ 210cd */
	default:
		br = 0xd9;
		break;
	case 16 ... 24: /* 240cd ~ 300cd */
		br = 0xd0;
		break;
	}

	s6e8aa0_dcs_write_seq(ctx, 0xd9, 0x14, 0x40, 0x0c, 0xcb, 0xce, 0x6e,
		0xc4, 0x0f, 0x40, 0x41, br, 0x00, 0x60, 0x19);
}

static void s6e8aa0_elvss_nvm_set(struct s6e8aa0 *ctx)
{
	if (ctx->version < 142)
		s6e8aa0_dcs_write_seq_static(ctx,
			0xd9, 0x14, 0x40, 0x0c, 0xcb, 0xce, 0x6e, 0xc4, 0x07,
			0x40, 0x41, 0xc1, 0x00, 0x60, 0x19);
	else
		s6e8aa0_elvss_nvm_set_v142(ctx);
};

static void s6e8aa0_apply_level_2_key(struct s6e8aa0 *ctx)
{
	s6e8aa0_dcs_write_seq_static(ctx, 0xfc, 0x5a, 0x5a);
}

static const s6e8aa0_gamma_table s6e8aa0_gamma_tables_v142[GAMMA_LEVEL_NUM] = {
	{
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x62, 0x55, 0x55,
		0xaf, 0xb1, 0xb1, 0xbd, 0xce, 0xb7, 0x9a, 0xb1,
		0x90, 0xb2, 0xc4, 0xae, 0x00, 0x60, 0x00, 0x40,
		0x00, 0x70,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x74, 0x68, 0x69,
		0xb8, 0xc1, 0xb7, 0xbd, 0xcd, 0xb8, 0x93, 0xab,
		0x88, 0xb4, 0xc4, 0xb1, 0x00, 0x6b, 0x00, 0x4d,
		0x00, 0x7d,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x95, 0x8a, 0x89,
		0xb4, 0xc6, 0xb2, 0xc5, 0xd2, 0xbf, 0x90, 0xa8,
		0x85, 0xb5, 0xc4, 0xb3, 0x00, 0x7b, 0x00, 0x5d,
		0x00, 0x8f,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x9f, 0x98, 0x92,
		0xb3, 0xc4, 0xb0, 0xbc, 0xcc, 0xb4, 0x91, 0xa6,
		0x87, 0xb5, 0xc5, 0xb4, 0x00, 0x87, 0x00, 0x6a,
		0x00, 0x9e,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x99, 0x93, 0x8b,
		0xb2, 0xc2, 0xb0, 0xbd, 0xce, 0xb4, 0x90, 0xa6,
		0x87, 0xb3, 0xc3, 0xb2, 0x00, 0x8d, 0x00, 0x70,
		0x00, 0xa4,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa7, 0xa5, 0x99,
		0xb2, 0xc2, 0xb0, 0xbb, 0xcd, 0xb1, 0x93, 0xa7,
		0x8a, 0xb2, 0xc1, 0xb0, 0x00, 0x92, 0x00, 0x75,
		0x00, 0xaa,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa0, 0xa0, 0x93,
		0xb6, 0xc4, 0xb4, 0xb5, 0xc8, 0xaa, 0x94, 0xa9,
		0x8c, 0xb2, 0xc0, 0xb0, 0x00, 0x97, 0x00, 0x7a,
		0x00, 0xaf,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa3, 0xa7, 0x96,
		0xb3, 0xc2, 0xb0, 0xba, 0xcb, 0xb0, 0x94, 0xa8,
		0x8c, 0xb0, 0xbf, 0xaf, 0x00, 0x9f, 0x00, 0x83,
		0x00, 0xb9,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x9d, 0xa2, 0x90,
		0xb6, 0xc5, 0xb3, 0xb8, 0xc9, 0xae, 0x94, 0xa8,
		0x8d, 0xaf, 0xbd, 0xad, 0x00, 0xa4, 0x00, 0x88,
		0x00, 0xbf,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa6, 0xac, 0x97,
		0xb4, 0xc4, 0xb1, 0xbb, 0xcb, 0xb2, 0x93, 0xa7,
		0x8d, 0xae, 0xbc, 0xad, 0x00, 0xa7, 0x00, 0x8c,
		0x00, 0xc3,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa2, 0xa9, 0x93,
		0xb6, 0xc5, 0xb2, 0xba, 0xc9, 0xb0, 0x93, 0xa7,
		0x8d, 0xae, 0xbb, 0xac, 0x00, 0xab, 0x00, 0x90,
		0x00, 0xc8,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0x9e, 0xa6, 0x8f,
		0xb7, 0xc6, 0xb3, 0xb8, 0xc8, 0xb0, 0x93, 0xa6,
		0x8c, 0xae, 0xbb, 0xad, 0x00, 0xae, 0x00, 0x93,
		0x00, 0xcc,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xab, 0xb4, 0x9c,
		0xb3, 0xc3, 0xaf, 0xb7, 0xc7, 0xaf, 0x93, 0xa6,
		0x8c, 0xaf, 0xbc, 0xad, 0x00, 0xb1, 0x00, 0x97,
		0x00, 0xcf,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa6, 0xb1, 0x98,
		0xb1, 0xc2, 0xab, 0xba, 0xc9, 0xb2, 0x93, 0xa6,
		0x8d, 0xae, 0xba, 0xab, 0x00, 0xb5, 0x00, 0x9b,
		0x00, 0xd4,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa3, 0xae, 0x94,
		0xb2, 0xc3, 0xac, 0xbb, 0xca, 0xb4, 0x91, 0xa4,
		0x8a, 0xae, 0xba, 0xac, 0x00, 0xb8, 0x00, 0x9e,
		0x00, 0xd8,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xab, 0xb7, 0x9c,
		0xae, 0xc0, 0xa9, 0xba, 0xc9, 0xb3, 0x92, 0xa5,
		0x8b, 0xad, 0xb9, 0xab, 0x00, 0xbb, 0x00, 0xa1,
		0x00, 0xdc,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa7, 0xb4, 0x97,
		0xb0, 0xc1, 0xaa, 0xb9, 0xc8, 0xb2, 0x92, 0xa5,
		0x8c, 0xae, 0xb9, 0xab, 0x00, 0xbe, 0x00, 0xa4,
		0x00, 0xdf,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa3, 0xb0, 0x94,
		0xb0, 0xc2, 0xab, 0xbb, 0xc9, 0xb3, 0x91, 0xa4,
		0x8b, 0xad, 0xb8, 0xaa, 0x00, 0xc1, 0x00, 0xa8,
		0x00, 0xe2,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa3, 0xb0, 0x94,
		0xae, 0xbf, 0xa8, 0xb9, 0xc8, 0xb3, 0x92, 0xa4,
		0x8b, 0xad, 0xb7, 0xa9, 0x00, 0xc4, 0x00, 0xab,
		0x00, 0xe6,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa7, 0xb6, 0x98,
		0xaf, 0xc0, 0xa8, 0xb8, 0xc7, 0xb2, 0x93, 0xa5,
		0x8d, 0xad, 0xb7, 0xa9, 0x00, 0xc7, 0x00, 0xae,
		0x00, 0xe9,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa4, 0xb3, 0x95,
		0xaf, 0xc1, 0xa9, 0xb9, 0xc8, 0xb3, 0x92, 0xa4,
		0x8b, 0xad, 0xb7, 0xaa, 0x00, 0xc9, 0x00, 0xb0,
		0x00, 0xec,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa4, 0xb3, 0x95,
		0xac, 0xbe, 0xa6, 0xbb, 0xc9, 0xb4, 0x90, 0xa3,
		0x8a, 0xad, 0xb7, 0xa9, 0x00, 0xcc, 0x00, 0xb4,
		0x00, 0xf0,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa0, 0xb0, 0x91,
		0xae, 0xc0, 0xa6, 0xba, 0xc8, 0xb4, 0x91, 0xa4,
		0x8b, 0xad, 0xb7, 0xa9, 0x00, 0xcf, 0x00, 0xb7,
		0x00, 0xf3,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa7, 0xb8, 0x98,
		0xab, 0xbd, 0xa4, 0xbb, 0xc9, 0xb5, 0x91, 0xa3,
		0x8b, 0xac, 0xb6, 0xa8, 0x00, 0xd1, 0x00, 0xb9,
		0x00, 0xf6,
	}, {
		0xfa, 0x01, 0x71, 0x31, 0x7b, 0xa4, 0xb5, 0x95,
		0xa9, 0xbc, 0xa1, 0xbb, 0xc9, 0xb5, 0x91, 0xa3,
		0x8a, 0xad, 0xb6, 0xa8, 0x00, 0xd6, 0x00, 0xbf,
		0x00, 0xfc,
	},
};

static const s6e8aa0_gamma_table s6e8aa0_gamma_tables_v96[GAMMA_LEVEL_NUM] = {
	{
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xdf, 0x1f, 0xd7, 0xdc, 0xb7, 0xe1, 0xc0, 0xaf,
		0xc4, 0xd2, 0xd0, 0xcf, 0x00, 0x4d, 0x00, 0x40,
		0x00, 0x5f,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd5, 0x35, 0xcf, 0xdc, 0xc1, 0xe1, 0xbf, 0xb3,
		0xc1, 0xd2, 0xd1, 0xce,	0x00, 0x53, 0x00, 0x46,
		0x00, 0x67,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd2, 0x64, 0xcf, 0xdb, 0xc6, 0xe1, 0xbd, 0xb3,
		0xbd, 0xd2, 0xd2, 0xce,	0x00, 0x59, 0x00, 0x4b,
		0x00, 0x6e,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd0, 0x7c, 0xcf, 0xdb, 0xc9, 0xe0, 0xbc, 0xb4,
		0xbb, 0xcf, 0xd1, 0xcc, 0x00, 0x5f, 0x00, 0x50,
		0x00, 0x75,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd0, 0x8e, 0xd1, 0xdb, 0xcc, 0xdf, 0xbb, 0xb6,
		0xb9, 0xd0, 0xd1, 0xcd,	0x00, 0x63, 0x00, 0x54,
		0x00, 0x7a,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd1, 0x9e, 0xd5, 0xda, 0xcd, 0xdd, 0xbb, 0xb7,
		0xb9, 0xce, 0xce, 0xc9,	0x00, 0x68, 0x00, 0x59,
		0x00, 0x81,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x00, 0xff,
		0xd0, 0xa5, 0xd6, 0xda, 0xcf, 0xdd, 0xbb, 0xb7,
		0xb8, 0xcc, 0xcd, 0xc7,	0x00, 0x6c, 0x00, 0x5c,
		0x00, 0x86,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x1f, 0xfe,
		0xd0, 0xae, 0xd7, 0xd9, 0xd0, 0xdb, 0xb9, 0xb6,
		0xb5, 0xca, 0xcc, 0xc5,	0x00, 0x74, 0x00, 0x63,
		0x00, 0x90,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x1f, 0xf9,
		0xcf, 0xb0, 0xd6, 0xd9, 0xd1, 0xdb, 0xb9, 0xb6,
		0xb4, 0xca, 0xcb, 0xc5,	0x00, 0x77, 0x00, 0x66,
		0x00, 0x94,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xff, 0x1f, 0xf7,
		0xcf, 0xb3, 0xd7, 0xd8, 0xd1, 0xd9, 0xb7, 0xb6,
		0xb3, 0xc9, 0xca, 0xc3,	0x00, 0x7b, 0x00, 0x69,
		0x00, 0x99,

	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xfd, 0x2f, 0xf7,
		0xdf, 0xb5, 0xd6, 0xd8, 0xd1, 0xd8, 0xb6, 0xb5,
		0xb2, 0xca, 0xcb, 0xc4,	0x00, 0x7e, 0x00, 0x6c,
		0x00, 0x9d,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xfa, 0x2f, 0xf5,
		0xce, 0xb6, 0xd5, 0xd7, 0xd2, 0xd8, 0xb6, 0xb4,
		0xb0, 0xc7, 0xc9, 0xc1,	0x00, 0x84, 0x00, 0x71,
		0x00, 0xa5,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xf7, 0x2f, 0xf2,
		0xce, 0xb9, 0xd5, 0xd8, 0xd2, 0xd8, 0xb4, 0xb4,
		0xaf, 0xc7, 0xc9, 0xc1,	0x00, 0x87, 0x00, 0x73,
		0x00, 0xa8,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xf5, 0x2f, 0xf0,
		0xdf, 0xba, 0xd5, 0xd7, 0xd2, 0xd7, 0xb4, 0xb4,
		0xaf, 0xc5, 0xc7, 0xbf,	0x00, 0x8a, 0x00, 0x76,
		0x00, 0xac,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xf2, 0x2f, 0xed,
		0xcE, 0xbb, 0xd4, 0xd6, 0xd2, 0xd6, 0xb5, 0xb4,
		0xaF, 0xc5, 0xc7, 0xbf,	0x00, 0x8c, 0x00, 0x78,
		0x00, 0xaf,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xef, 0x2f, 0xeb,
		0xcd, 0xbb, 0xd2, 0xd7, 0xd3, 0xd6, 0xb3, 0xb4,
		0xae, 0xc5, 0xc6, 0xbe,	0x00, 0x91, 0x00, 0x7d,
		0x00, 0xb6,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xee, 0x2f, 0xea,
		0xce, 0xbd, 0xd4, 0xd6, 0xd2, 0xd5, 0xb2, 0xb3,
		0xad, 0xc3, 0xc4, 0xbb,	0x00, 0x94, 0x00, 0x7f,
		0x00, 0xba,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xec, 0x2f, 0xe8,
		0xce, 0xbe, 0xd3, 0xd6, 0xd3, 0xd5, 0xb2, 0xb2,
		0xac, 0xc3, 0xc5, 0xbc,	0x00, 0x96, 0x00, 0x81,
		0x00, 0xbd,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xeb, 0x2f, 0xe7,
		0xce, 0xbf, 0xd3, 0xd6, 0xd2, 0xd5, 0xb1, 0xb2,
		0xab, 0xc2, 0xc4, 0xbb,	0x00, 0x99, 0x00, 0x83,
		0x00, 0xc0,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xef, 0x5f, 0xe9,
		0xca, 0xbf, 0xd3, 0xd5, 0xd2, 0xd4, 0xb2, 0xb2,
		0xab, 0xc1, 0xc4, 0xba,	0x00, 0x9b, 0x00, 0x85,
		0x00, 0xc3,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xea, 0x5f, 0xe8,
		0xee, 0xbf, 0xd2, 0xd5, 0xd2, 0xd4, 0xb1, 0xb2,
		0xab, 0xc1, 0xc2, 0xb9,	0x00, 0x9D, 0x00, 0x87,
		0x00, 0xc6,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xe9, 0x5f, 0xe7,
		0xcd, 0xbf, 0xd2, 0xd6, 0xd2, 0xd4, 0xb1, 0xb2,
		0xab, 0xbe, 0xc0, 0xb7,	0x00, 0xa1, 0x00, 0x8a,
		0x00, 0xca,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xe8, 0x61, 0xe6,
		0xcd, 0xbf, 0xd1, 0xd6, 0xd3, 0xd4, 0xaf, 0xb0,
		0xa9, 0xbe, 0xc1, 0xb7,	0x00, 0xa3, 0x00, 0x8b,
		0x00, 0xce,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xe8, 0x62, 0xe5,
		0xcc, 0xc0, 0xd0, 0xd6, 0xd2, 0xd4, 0xaf, 0xb1,
		0xa9, 0xbd, 0xc0, 0xb6,	0x00, 0xa5, 0x00, 0x8d,
		0x00, 0xd0,
	}, {
		0xfa, 0x01, 0x1f, 0x1f, 0x1f, 0xe7, 0x7f, 0xe3,
		0xcc, 0xc1, 0xd0, 0xd5, 0xd3, 0xd3, 0xae, 0xaf,
		0xa8, 0xbe, 0xc0, 0xb7,	0x00, 0xa8, 0x00, 0x90,
		0x00, 0xd3,
	}
};

static const s6e8aa0_gamma_table s6e8aa0_gamma_tables_v32[GAMMA_LEVEL_NUM] = {
	{
		0xfa, 0x01, 0x43, 0x14, 0x45, 0x72, 0x5e, 0x6b,
		0xa1, 0xa7, 0x9a, 0xb4, 0xcb, 0xb8, 0x92, 0xac,
		0x97, 0xb4, 0xc3, 0xb5, 0x00, 0x4e, 0x00, 0x37,
		0x00, 0x58,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0x85, 0x71, 0x7d,
		0xa6, 0xb6, 0xa1, 0xb5, 0xca, 0xba, 0x93, 0xac,
		0x98, 0xb2, 0xc0, 0xaf, 0x00, 0x59, 0x00, 0x43,
		0x00, 0x64,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xa4, 0x94, 0x9e,
		0xa0, 0xbb, 0x9c, 0xc3, 0xd2, 0xc6, 0x93, 0xaa,
		0x95, 0xb7, 0xc2, 0xb4, 0x00, 0x65, 0x00, 0x50,
		0x00, 0x74,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xa1, 0xa6,
		0xa0, 0xb9, 0x9b, 0xc3, 0xd1, 0xc8, 0x90, 0xa6,
		0x90, 0xbb, 0xc3, 0xb7, 0x00, 0x6f, 0x00, 0x5b,
		0x00, 0x80,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xa6, 0x9d, 0x9f,
		0x9f, 0xb8, 0x9a, 0xc7, 0xd5, 0xcc, 0x90, 0xa5,
		0x8f, 0xb8, 0xc1, 0xb6, 0x00, 0x74, 0x00, 0x60,
		0x00, 0x85,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb3, 0xae, 0xae,
		0x9e, 0xb7, 0x9a, 0xc8, 0xd6, 0xce, 0x91, 0xa6,
		0x90, 0xb6, 0xc0, 0xb3, 0x00, 0x78, 0x00, 0x65,
		0x00, 0x8a,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xa9, 0xa8,
		0xa3, 0xb9, 0x9e, 0xc4, 0xd3, 0xcb, 0x94, 0xa6,
		0x90, 0xb6, 0xbf, 0xb3, 0x00, 0x7c, 0x00, 0x69,
		0x00, 0x8e,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xaf, 0xaf, 0xa9,
		0xa5, 0xbc, 0xa2, 0xc7, 0xd5, 0xcd, 0x93, 0xa5,
		0x8f, 0xb4, 0xbd, 0xb1, 0x00, 0x83, 0x00, 0x70,
		0x00, 0x96,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xa9, 0xab, 0xa3,
		0xaa, 0xbf, 0xa7, 0xc5, 0xd3, 0xcb, 0x93, 0xa5,
		0x8f, 0xb2, 0xbb, 0xb0, 0x00, 0x86, 0x00, 0x74,
		0x00, 0x9b,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb1, 0xb5, 0xab,
		0xab, 0xc0, 0xa9, 0xc7, 0xd4, 0xcc, 0x94, 0xa4,
		0x8f, 0xb1, 0xbb, 0xaf, 0x00, 0x8a, 0x00, 0x77,
		0x00, 0x9e,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xb2, 0xa7,
		0xae, 0xc2, 0xab, 0xc5, 0xd3, 0xca, 0x93, 0xa4,
		0x8f, 0xb1, 0xba, 0xae, 0x00, 0x8d, 0x00, 0x7b,
		0x00, 0xa2,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xa9, 0xaf, 0xa3,
		0xb0, 0xc3, 0xae, 0xc4, 0xd1, 0xc8, 0x93, 0xa4,
		0x8f, 0xb1, 0xba, 0xaf, 0x00, 0x8f, 0x00, 0x7d,
		0x00, 0xa5,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb4, 0xbd, 0xaf,
		0xae, 0xc1, 0xab, 0xc2, 0xd0, 0xc6, 0x94, 0xa4,
		0x8f, 0xb1, 0xba, 0xaf, 0x00, 0x92, 0x00, 0x80,
		0x00, 0xa8,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb0, 0xb9, 0xac,
		0xad, 0xc1, 0xab, 0xc4, 0xd1, 0xc7, 0x95, 0xa4,
		0x90, 0xb0, 0xb9, 0xad, 0x00, 0x95, 0x00, 0x84,
		0x00, 0xac,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xb6, 0xa7,
		0xaf, 0xc2, 0xae, 0xc5, 0xd1, 0xc7, 0x93, 0xa3,
		0x8e, 0xb0, 0xb9, 0xad, 0x00, 0x98, 0x00, 0x86,
		0x00, 0xaf,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb4, 0xbf, 0xaf,
		0xad, 0xc1, 0xab, 0xc3, 0xd0, 0xc6, 0x94, 0xa3,
		0x8f, 0xaf, 0xb8, 0xac, 0x00, 0x9a, 0x00, 0x89,
		0x00, 0xb2,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb0, 0xbc, 0xac,
		0xaf, 0xc2, 0xad, 0xc2, 0xcf, 0xc4, 0x94, 0xa3,
		0x90, 0xaf, 0xb8, 0xad, 0x00, 0x9c, 0x00, 0x8b,
		0x00, 0xb5,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xb9, 0xa7,
		0xb1, 0xc4, 0xaf, 0xc3, 0xcf, 0xc5, 0x94, 0xa3,
		0x8f, 0xae, 0xb7, 0xac, 0x00, 0x9f, 0x00, 0x8e,
		0x00, 0xb8,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xb9, 0xa7,
		0xaf, 0xc2, 0xad, 0xc1, 0xce, 0xc3, 0x95, 0xa3,
		0x90, 0xad, 0xb6, 0xab, 0x00, 0xa2, 0x00, 0x91,
		0x00, 0xbb,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb1, 0xbe, 0xac,
		0xb1, 0xc4, 0xaf, 0xc1, 0xcd, 0xc1, 0x95, 0xa4,
		0x91, 0xad, 0xb6, 0xab, 0x00, 0xa4, 0x00, 0x93,
		0x00, 0xbd,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xbb, 0xa8,
		0xb3, 0xc5, 0xb2, 0xc1, 0xcd, 0xc2, 0x95, 0xa3,
		0x90, 0xad, 0xb6, 0xab, 0x00, 0xa6, 0x00, 0x95,
		0x00, 0xc0,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xbb, 0xa8,
		0xb0, 0xc3, 0xaf, 0xc2, 0xce, 0xc2, 0x94, 0xa2,
		0x90, 0xac, 0xb6, 0xab, 0x00, 0xa8, 0x00, 0x98,
		0x00, 0xc3,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xa9, 0xb8, 0xa5,
		0xb3, 0xc5, 0xb2, 0xc1, 0xcc, 0xc0, 0x95, 0xa2,
		0x90, 0xad, 0xb6, 0xab, 0x00, 0xaa, 0x00, 0x9a,
		0x00, 0xc5,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xb0, 0xc0, 0xac,
		0xb0, 0xc3, 0xaf, 0xc1, 0xcd, 0xc1, 0x95, 0xa2,
		0x90, 0xac, 0xb5, 0xa9, 0x00, 0xac, 0x00, 0x9c,
		0x00, 0xc8,
	}, {
		0xfa, 0x01, 0x43, 0x14, 0x45, 0xad, 0xbd, 0xa8,
		0xaf, 0xc2, 0xaf, 0xc1, 0xcc, 0xc0, 0x95, 0xa2,
		0x90, 0xac, 0xb5, 0xaa, 0x00, 0xb1, 0x00, 0xa1,
		0x00, 0xcc,
	},
};

static const struct s6e8aa0_variant s6e8aa0_variants[] = {
	{
		.version = 32,
		.gamma_tables = s6e8aa0_gamma_tables_v32,
	}, {
		.version = 96,
		.gamma_tables = s6e8aa0_gamma_tables_v96,
	}, {
		.version = 142,
		.gamma_tables = s6e8aa0_gamma_tables_v142,
	}, {
		.version = 210,
		.gamma_tables = s6e8aa0_gamma_tables_v142,
	}
};

static void s6e8aa0_brightness_set(struct s6e8aa0 *ctx)
{
	const u8 *gamma;

	if (ctx->error)
		return;

	gamma = ctx->variant->gamma_tables[ctx->brightness];

	if (ctx->version >= 142)
		s6e8aa0_elvss_nvm_set(ctx);

	s6e8aa0_dcs_write(ctx, gamma, GAMMA_TABLE_LEN);

	/* update gamma table. */
	s6e8aa0_dcs_write_seq_static(ctx, 0xf7, 0x03);
}

static void s6e8aa0_panel_init(struct s6e8aa0 *ctx)
{
	s6e8aa0_apply_level_1_key(ctx);
	s6e8aa0_apply_level_2_key(ctx);
	msleep(20);

	s6e8aa0_dcs_write_seq_static(ctx, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(40);

	s6e8aa0_panel_cond_set(ctx);
	s6e8aa0_display_condition_set(ctx);
	s6e8aa0_brightness_set(ctx);
	s6e8aa0_etc_source_control(ctx);
	s6e8aa0_etc_pentile_control(ctx);
	s6e8aa0_elvss_nvm_set(ctx);
	s6e8aa0_etc_power_control(ctx);
	s6e8aa0_etc_elvss_control(ctx);
	msleep(ctx->init_delay);
}

static void s6e8aa0_set_maximum_return_packet_size(struct s6e8aa0 *ctx,
						   int size)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;
	u8 buf[] = {size, 0};
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
		.tx_len = sizeof(buf),
		.tx_buf = buf
	};
	int ret;

	if (ctx->error < 0)
		return;

	if (!ops || !ops->transfer)
		ret = -EIO;
	else
		ret = ops->transfer(dsi->host, &msg);

	if (ret < 0) {
		dev_err(ctx->dev,
			"error %d setting maximum return packet size to %d\n",
			ret, size);
		ctx->error = ret;
	}
}

static void s6e8aa0_read_mtp_id(struct s6e8aa0 *ctx)
{
	u8 id[3];
	int ret, i;

	ret = s6e8aa0_dcs_read(ctx, 0xd1, id, ARRAY_SIZE(id));
	if (ret < ARRAY_SIZE(id) || id[0] == 0x00) {
		dev_err(ctx->dev, "read id failed\n");
		ctx->error = -EIO;
		return;
	}

	dev_info(ctx->dev, "ID: 0x%2x, 0x%2x, 0x%2x\n", id[0], id[1], id[2]);

	for (i = 0; i < ARRAY_SIZE(s6e8aa0_variants); ++i) {
		if (id[1] == s6e8aa0_variants[i].version)
			break;
	}
	if (i >= ARRAY_SIZE(s6e8aa0_variants)) {
		dev_err(ctx->dev, "unsupported display version %d\n", id[1]);
		ctx->error = -EINVAL;
		return;
	}

	ctx->variant = &s6e8aa0_variants[i];
	ctx->version = id[1];
	ctx->id = id[2];
}

static void s6e8aa0_set_sequence(struct s6e8aa0 *ctx)
{
	s6e8aa0_set_maximum_return_packet_size(ctx, 3);
	s6e8aa0_read_mtp_id(ctx);
	s6e8aa0_panel_init(ctx);
	s6e8aa0_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_ON);
}

static int s6e8aa0_power_on(struct s6e8aa0 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(ctx->power_on_delay);

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value(ctx->reset_gpio, 1);

	msleep(ctx->reset_delay);

	return 0;
}

static int s6e8aa0_power_off(struct s6e8aa0 *ctx)
{
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int s6e8aa0_disable(struct drm_panel *panel)
{
	struct s6e8aa0 *ctx = panel_to_s6e8aa0(panel);

	s6e8aa0_dcs_write_seq_static(ctx, MIPI_DCS_ENTER_SLEEP_MODE);
	s6e8aa0_dcs_write_seq_static(ctx, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(40);

	s6e8aa0_clear_error(ctx);

	return s6e8aa0_power_off(ctx);
}

static int s6e8aa0_enable(struct drm_panel *panel)
{
	struct s6e8aa0 *ctx = panel_to_s6e8aa0(panel);
	int ret;

	ret = s6e8aa0_power_on(ctx);
	if (ret < 0)
		return ret;

	s6e8aa0_set_sequence(ctx);
	ret = ctx->error;

	if (ret < 0)
		s6e8aa0_disable(panel);

	return ret;
}

static int s6e8aa0_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct s6e8aa0 *ctx = panel_to_s6e8aa0(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_ERROR("failed to create a new display mode\n");
		return 0;
	}

	drm_display_mode_from_videomode(&ctx->vm, mode);
	mode->width_mm = ctx->width_mm;
	mode->height_mm = ctx->height_mm;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6e8aa0_drm_funcs = {
	.disable = s6e8aa0_disable,
	.enable = s6e8aa0_enable,
	.get_modes = s6e8aa0_get_modes,
};

static int s6e8aa0_parse_dt(struct s6e8aa0 *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *np = dev->of_node;
	int ret;

	ret = of_get_videomode(np, &ctx->vm, 0);
	if (ret < 0)
		return ret;

	of_property_read_u32(np, "power-on-delay", &ctx->power_on_delay);
	of_property_read_u32(np, "reset-delay", &ctx->reset_delay);
	of_property_read_u32(np, "init-delay", &ctx->init_delay);
	of_property_read_u32(np, "panel-width-mm", &ctx->width_mm);
	of_property_read_u32(np, "panel-height-mm", &ctx->height_mm);

	ctx->flip_horizontal = of_property_read_bool(np, "flip-horizontal");
	ctx->flip_vertical = of_property_read_bool(np, "flip-vertical");

	return 0;
}

static int s6e8aa0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e8aa0 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct s6e8aa0), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| MIPI_DSI_MODE_VIDEO_HFP | MIPI_DSI_MODE_VIDEO_HBP
		| MIPI_DSI_MODE_VIDEO_HSA | MIPI_DSI_MODE_EOT_PACKET
		| MIPI_DSI_MODE_VSYNC_FLUSH | MIPI_DSI_MODE_VIDEO_AUTO_VERT;

	ret = s6e8aa0_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ctx->supplies[0].supply = "vdd3";
	ctx->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset");
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpios %ld\n",
			PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	ret = gpiod_direction_output(ctx->reset_gpio, 1);
	if (ret < 0) {
		dev_err(dev, "cannot configure reset-gpios %d\n", ret);
		return ret;
	}

	ctx->brightness = GAMMA_LEVEL_NUM - 1;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &s6e8aa0_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static int s6e8aa0_remove(struct mipi_dsi_device *dsi)
{
	struct s6e8aa0 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static struct of_device_id s6e8aa0_of_match[] = {
	{ .compatible = "samsung,s6e8aa0" },
	{ }
};
MODULE_DEVICE_TABLE(of, s6e8aa0_of_match);

static struct mipi_dsi_driver s6e8aa0_driver = {
	.probe = s6e8aa0_probe,
	.remove = s6e8aa0_remove,
	.driver = {
		.name = "panel_s6e8aa0",
		.owner = THIS_MODULE,
		.of_match_table = s6e8aa0_of_match,
	},
};
module_mipi_dsi_driver(s6e8aa0_driver);

MODULE_AUTHOR("Donghwa Lee <dh09.lee@samsung.com>");
MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joongmock Shin <jmock.shin@samsung.com>");
MODULE_AUTHOR("Eunchul Kim <chulspro.kim@samsung.com>");
MODULE_AUTHOR("Tomasz Figa <t.figa@samsung.com>");
MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based s6e8aa0 AMOLED LCD Panel Driver");
MODULE_LICENSE("GPL v2");
