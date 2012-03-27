/*
 * Flash support for OMAP1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>

#include <plat/tc.h>
#include <plat/flash.h>

#include <mach/hardware.h>

void omap1_set_vpp(struct platform_device *pdev, int enable)
{
	static int count;
	u32 l;

	if (enable) {
		if (count++ == 0) {
			l = omap_readl(EMIFS_CONFIG);
			l |= OMAP_EMIFS_CONFIG_WP;
			omap_writel(l, EMIFS_CONFIG);
		}
	} else {
		if (count && (--count == 0)) {
			l = omap_readl(EMIFS_CONFIG);
			l &= ~OMAP_EMIFS_CONFIG_WP;
			omap_writel(l, EMIFS_CONFIG);
		}
	}
}
