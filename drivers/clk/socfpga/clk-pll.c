/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based from clk-highbank.c
 *
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>

#include "clk.h"

/* Clock bypass bits */
#define MAINPLL_BYPASS		(1<<0)
#define SDRAMPLL_BYPASS		(1<<1)
#define SDRAMPLL_SRC_BYPASS	(1<<2)
#define PERPLL_BYPASS		(1<<3)
#define PERPLL_SRC_BYPASS	(1<<4)

#define SOCFPGA_PLL_BG_PWRDWN		0
#define SOCFPGA_PLL_EXT_ENA		1
#define SOCFPGA_PLL_PWR_DOWN		2
#define SOCFPGA_PLL_DIVF_MASK		0x0000FFF8
#define SOCFPGA_PLL_DIVF_SHIFT		3
#define SOCFPGA_PLL_DIVQ_MASK		0x003F0000
#define SOCFPGA_PLL_DIVQ_SHIFT		16

#define to_socfpga_clk(p) container_of(p, struct socfpga_pll, hw.hw)

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_pll *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, reg;
	unsigned long long vco_freq;
	unsigned long bypass;

	reg = readl(socfpgaclk->hw.reg);
	bypass = readl(clk_mgr_base_addr + CLKMGR_BYPASS);
	if (bypass & MAINPLL_BYPASS)
		return parent_rate;

	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = (unsigned long long)parent_rate * (divf + 1);
	do_div(vco_freq, (1 + divq));
	return (unsigned long)vco_freq;
}

static struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
};

static __init struct clk *__socfpga_pll_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_pll *pll_clk;
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_init_data init;
	int rc;

	of_property_read_u32(node, "reg", &reg);

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (WARN_ON(!pll_clk))
		return NULL;

	pll_clk->hw.reg = clk_mgr_base_addr + reg;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	pll_clk->hw.hw.init = &init;

	pll_clk->hw.bit_idx = SOCFPGA_PLL_EXT_ENA;
	clk_pll_ops.enable = clk_gate_ops.enable;
	clk_pll_ops.disable = clk_gate_ops.disable;

	clk = clk_register(NULL, &pll_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(pll_clk);
		return NULL;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return clk;
}

void __init socfpga_pll_init(struct device_node *node)
{
	__socfpga_pll_init(node, &clk_pll_ops);
}
