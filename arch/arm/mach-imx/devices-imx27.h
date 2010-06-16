/*
 * Copyright (C) 2010 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <mach/mx27.h>
#include <mach/devices-common.h>

#define imx27_add_i2c_imx0(pdata)	\
	imx_add_imx_i2c(0, MX27_I2C1_BASE_ADDR, SZ_4K, MX27_INT_I2C1, pdata)
#define imx27_add_i2c_imx1(pdata)	\
	imx_add_imx_i2c(1, MX27_I2C2_BASE_ADDR, SZ_4K, MX27_INT_I2C2, pdata)

#define imx27_add_mxc_nand(pdata)	\
	imx_add_mxc_nand_v1(MX27_NFC_BASE_ADDR, MX27_INT_NANDFC, pdata)
