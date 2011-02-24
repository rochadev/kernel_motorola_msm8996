/*
 * omap_hwmod_2430_data.c - hardware modules present on the OMAP2430 chips
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>
#include <plat/serial.h>
#include <plat/i2c.h>
#include <plat/gpio.h>
#include <plat/mcbsp.h>
#include <plat/mcspi.h>
#include <plat/l3_2xxx.h>

#include "omap_hwmod_common_data.h"

#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "wd_timer.h"

/*
 * OMAP2430 hardware module integration data
 *
 * ALl of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

static struct omap_hwmod omap2430_mpu_hwmod;
static struct omap_hwmod omap2430_iva_hwmod;
static struct omap_hwmod omap2430_l3_main_hwmod;
static struct omap_hwmod omap2430_l4_core_hwmod;
static struct omap_hwmod omap2430_dss_core_hwmod;
static struct omap_hwmod omap2430_dss_dispc_hwmod;
static struct omap_hwmod omap2430_dss_rfbi_hwmod;
static struct omap_hwmod omap2430_dss_venc_hwmod;
static struct omap_hwmod omap2430_wd_timer2_hwmod;
static struct omap_hwmod omap2430_gpio1_hwmod;
static struct omap_hwmod omap2430_gpio2_hwmod;
static struct omap_hwmod omap2430_gpio3_hwmod;
static struct omap_hwmod omap2430_gpio4_hwmod;
static struct omap_hwmod omap2430_gpio5_hwmod;
static struct omap_hwmod omap2430_dma_system_hwmod;
static struct omap_hwmod omap2430_mcbsp1_hwmod;
static struct omap_hwmod omap2430_mcbsp2_hwmod;
static struct omap_hwmod omap2430_mcbsp3_hwmod;
static struct omap_hwmod omap2430_mcbsp4_hwmod;
static struct omap_hwmod omap2430_mcbsp5_hwmod;
static struct omap_hwmod omap2430_mcspi1_hwmod;
static struct omap_hwmod omap2430_mcspi2_hwmod;
static struct omap_hwmod omap2430_mcspi3_hwmod;

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap2430_l3_main__l4_core = {
	.master	= &omap2430_l3_main_hwmod,
	.slave	= &omap2430_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap2430_mpu__l3_main = {
	.master = &omap2430_mpu_hwmod,
	.slave	= &omap2430_l3_main_hwmod,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2430_l3_main_slaves[] = {
	&omap2430_mpu__l3_main,
};

/* DSS -> l3 */
static struct omap_hwmod_ocp_if omap2430_dss__l3 = {
	.master		= &omap2430_dss_core_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.fw = {
		.omap2 = {
			.l3_perm_bit  = OMAP2_L3_CORE_FW_CONNID_DSS,
			.flags	= OMAP_FIREWALL_L3,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* Master interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2430_l3_main_masters[] = {
	&omap2430_l3_main__l4_core,
};

/* L3 */
static struct omap_hwmod omap2430_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.masters	= omap2430_l3_main_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l3_main_masters),
	.slaves		= omap2430_l3_main_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l3_main_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod omap2430_l4_wkup_hwmod;
static struct omap_hwmod omap2430_uart1_hwmod;
static struct omap_hwmod omap2430_uart2_hwmod;
static struct omap_hwmod omap2430_uart3_hwmod;
static struct omap_hwmod omap2430_i2c1_hwmod;
static struct omap_hwmod omap2430_i2c2_hwmod;

static struct omap_hwmod omap2430_usbhsotg_hwmod;

/* l3_core -> usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_usbhsotg__l3 = {
	.master		= &omap2430_usbhsotg_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU,
};

/* I2C IP block address space length (in bytes) */
#define OMAP2_I2C_AS_LEN		128

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_addr_space omap2430_i2c1_addr_space[] = {
	{
		.pa_start	= 0x48070000,
		.pa_end		= 0x48070000 + OMAP2_I2C_AS_LEN - 1,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__i2c1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.addr		= omap2430_i2c1_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_i2c1_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_addr_space omap2430_i2c2_addr_space[] = {
	{
		.pa_start	= 0x48072000,
		.pa_end		= 0x48072000 + OMAP2_I2C_AS_LEN - 1,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__i2c2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.addr		= omap2430_i2c2_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_i2c2_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__l4_wkup = {
	.master	= &omap2430_l4_core_hwmod,
	.slave	= &omap2430_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART1 interface */
static struct omap_hwmod_addr_space omap2430_uart1_addr_space[] = {
	{
		.pa_start	= OMAP2_UART1_BASE,
		.pa_end		= OMAP2_UART1_BASE + SZ_8K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2_l4_core__uart1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart1_hwmod,
	.clk		= "uart1_ick",
	.addr		= omap2430_uart1_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_uart1_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */
static struct omap_hwmod_addr_space omap2430_uart2_addr_space[] = {
	{
		.pa_start	= OMAP2_UART2_BASE,
		.pa_end		= OMAP2_UART2_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2_l4_core__uart2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart2_hwmod,
	.clk		= "uart2_ick",
	.addr		= omap2430_uart2_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_uart2_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */
static struct omap_hwmod_addr_space omap2430_uart3_addr_space[] = {
	{
		.pa_start	= OMAP2_UART3_BASE,
		.pa_end		= OMAP2_UART3_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2_l4_core__uart3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart3_hwmod,
	.clk		= "uart3_ick",
	.addr		= omap2430_uart3_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_uart3_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
* usbhsotg interface data
*/
static struct omap_hwmod_addr_space omap2430_usbhsotg_addrs[] = {
	{
		.pa_start	= OMAP243X_HS_BASE,
		.pa_end		= OMAP243X_HS_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
};

/*  l4_core ->usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__usbhsotg = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_usbhsotg_hwmod,
	.clk		= "usb_l4_ick",
	.addr		= omap2430_usbhsotg_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_usbhsotg_addrs),
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if *omap2430_usbhsotg_masters[] = {
	&omap2430_usbhsotg__l3,
};

static struct omap_hwmod_ocp_if *omap2430_usbhsotg_slaves[] = {
	&omap2430_l4_core__usbhsotg,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_slaves[] = {
	&omap2430_l3_main__l4_core,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_masters[] = {
	&omap2430_l4_core__l4_wkup,
};

/* L4 CORE */
static struct omap_hwmod omap2430_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_core_masters),
	.slaves		= omap2430_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_core_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_slaves[] = {
	&omap2430_l4_core__l4_wkup,
	&omap2_l4_core__uart1,
	&omap2_l4_core__uart2,
	&omap2_l4_core__uart3,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_masters[] = {
};

/* l4 core -> mcspi1 interface */
static struct omap_hwmod_addr_space omap2430_mcspi1_addr_space[] = {
	{
		.pa_start	= 0x48098000,
		.pa_end		= 0x480980ff,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi1_hwmod,
	.clk		= "mcspi1_ick",
	.addr		= omap2430_mcspi1_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcspi1_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
static struct omap_hwmod_addr_space omap2430_mcspi2_addr_space[] = {
	{
		.pa_start	= 0x4809a000,
		.pa_end		= 0x4809a0ff,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi2_hwmod,
	.clk		= "mcspi2_ick",
	.addr		= omap2430_mcspi2_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcspi2_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_addr_space omap2430_mcspi3_addr_space[] = {
	{
		.pa_start	= 0x480b8000,
		.pa_end		= 0x480b80ff,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi3_hwmod,
	.clk		= "mcspi3_ick",
	.addr		= omap2430_mcspi3_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcspi3_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 WKUP */
static struct omap_hwmod omap2430_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_wkup_masters),
	.slaves		= omap2430_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_wkup_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Master interfaces on the MPU device */
static struct omap_hwmod_ocp_if *omap2430_mpu_masters[] = {
	&omap2430_mpu__l3_main,
};

/* MPU */
static struct omap_hwmod omap2430_mpu_hwmod = {
	.name		= "mpu",
	.class		= &mpu_hwmod_class,
	.main_clk	= "mpu_ck",
	.masters	= omap2430_mpu_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_mpu_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/*
 * IVA2_1 interface data
 */

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap2430_l3__iva = {
	.master		= &omap2430_l3_main_hwmod,
	.slave		= &omap2430_iva_hwmod,
	.clk		= "dsp_fck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap2430_iva_masters[] = {
	&omap2430_l3__iva,
};

/*
 * IVA2 (IVA2)
 */

static struct omap_hwmod omap2430_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.masters	= omap2430_iva_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_iva_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430)
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap2430_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x49016000,
		.pa_end		= 0x4901607f,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__wd_timer2 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_wd_timer2_hwmod,
	.clk		= "mpu_wdt_ick",
	.addr		= omap2430_wd_timer2_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_wd_timer2_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap2430_wd_timer_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_EMUFREE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &omap2430_wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable
};

/* wd_timer2 */
static struct omap_hwmod_ocp_if *omap2430_wd_timer2_slaves[] = {
	&omap2430_l4_wkup__wd_timer2,
};

static struct omap_hwmod omap2430_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap2430_wd_timer_hwmod_class,
	.main_clk	= "mpu_wdt_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MPU_WDT_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MPU_WDT_SHIFT,
		},
	},
	.slaves		= omap2430_wd_timer2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_wd_timer2_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* UART */

static struct omap_hwmod_class_sysconfig uart_sysc = {
	.rev_offs	= 0x50,
	.sysc_offs	= 0x54,
	.syss_offs	= 0x58,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class uart_class = {
	.name = "uart",
	.sysc = &uart_sysc,
};

/* UART1 */

static struct omap_hwmod_irq_info uart1_mpu_irqs[] = {
	{ .irq = INT_24XX_UART1_IRQ, },
};

static struct omap_hwmod_dma_info uart1_sdma_reqs[] = {
	{ .name = "rx",	.dma_req = OMAP24XX_DMA_UART1_RX, },
	{ .name = "tx",	.dma_req = OMAP24XX_DMA_UART1_TX, },
};

static struct omap_hwmod_ocp_if *omap2430_uart1_slaves[] = {
	&omap2_l4_core__uart1,
};

static struct omap_hwmod omap2430_uart1_hwmod = {
	.name		= "uart1",
	.mpu_irqs	= uart1_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(uart1_mpu_irqs),
	.sdma_reqs	= uart1_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(uart1_sdma_reqs),
	.main_clk	= "uart1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART1_SHIFT,
		},
	},
	.slaves		= omap2430_uart1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart1_slaves),
	.class		= &uart_class,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* UART2 */

static struct omap_hwmod_irq_info uart2_mpu_irqs[] = {
	{ .irq = INT_24XX_UART2_IRQ, },
};

static struct omap_hwmod_dma_info uart2_sdma_reqs[] = {
	{ .name = "rx",	.dma_req = OMAP24XX_DMA_UART2_RX, },
	{ .name = "tx",	.dma_req = OMAP24XX_DMA_UART2_TX, },
};

static struct omap_hwmod_ocp_if *omap2430_uart2_slaves[] = {
	&omap2_l4_core__uart2,
};

static struct omap_hwmod omap2430_uart2_hwmod = {
	.name		= "uart2",
	.mpu_irqs	= uart2_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(uart2_mpu_irqs),
	.sdma_reqs	= uart2_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(uart2_sdma_reqs),
	.main_clk	= "uart2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART2_SHIFT,
		},
	},
	.slaves		= omap2430_uart2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart2_slaves),
	.class		= &uart_class,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* UART3 */

static struct omap_hwmod_irq_info uart3_mpu_irqs[] = {
	{ .irq = INT_24XX_UART3_IRQ, },
};

static struct omap_hwmod_dma_info uart3_sdma_reqs[] = {
	{ .name = "rx",	.dma_req = OMAP24XX_DMA_UART3_RX, },
	{ .name = "tx",	.dma_req = OMAP24XX_DMA_UART3_TX, },
};

static struct omap_hwmod_ocp_if *omap2430_uart3_slaves[] = {
	&omap2_l4_core__uart3,
};

static struct omap_hwmod omap2430_uart3_hwmod = {
	.name		= "uart3",
	.mpu_irqs	= uart3_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(uart3_mpu_irqs),
	.sdma_reqs	= uart3_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(uart3_sdma_reqs),
	.main_clk	= "uart3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP24XX_EN_UART3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP24XX_EN_UART3_SHIFT,
		},
	},
	.slaves		= omap2430_uart3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart3_slaves),
	.class		= &uart_class,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/*
 * 'dss' class
 * display sub-system
 */

static struct omap_hwmod_class_sysconfig omap2430_dss_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_dss_hwmod_class = {
	.name = "dss",
	.sysc = &omap2430_dss_sysc,
};

/* dss */
static struct omap_hwmod_irq_info omap2430_dss_irqs[] = {
	{ .irq = 25 },
};
static struct omap_hwmod_dma_info omap2430_dss_sdma_chs[] = {
	{ .name = "dispc", .dma_req = 5 },
};

/* dss */
/* dss master ports */
static struct omap_hwmod_ocp_if *omap2430_dss_masters[] = {
	&omap2430_dss__l3,
};

static struct omap_hwmod_addr_space omap2430_dss_addrs[] = {
	{
		.pa_start	= 0x48050000,
		.pa_end		= 0x480503FF,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> dss */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2430_dss_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_dss_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_slaves[] = {
	&omap2430_l4_core__dss,
};

static struct omap_hwmod_opt_clk dss_opt_clks[] = {
	{ .role = "tv_clk", .clk = "dss_54m_fck" },
	{ .role = "sys_clk", .clk = "dss2_fck" },
};

static struct omap_hwmod omap2430_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2430_dss_hwmod_class,
	.main_clk	= "dss1_fck", /* instead of dss_fck */
	.mpu_irqs	= omap2430_dss_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_dss_irqs),
	.sdma_reqs	= omap2430_dss_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_dss_sdma_chs),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.slaves		= omap2430_dss_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_slaves),
	.masters	= omap2430_dss_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_dss_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'dispc' class
 * display controller
 */

static struct omap_hwmod_class_sysconfig omap2430_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_dispc_hwmod_class = {
	.name = "dispc",
	.sysc = &omap2430_dispc_sysc,
};

static struct omap_hwmod_addr_space omap2430_dss_dispc_addrs[] = {
	{
		.pa_start	= 0x48050400,
		.pa_end		= 0x480507FF,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> dss_dispc */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_dispc = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2430_dss_dispc_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_dss_dispc_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_dispc slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_dispc_slaves[] = {
	&omap2430_l4_core__dss_dispc,
};

static struct omap_hwmod omap2430_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &omap2430_dispc_hwmod_class,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.slaves		= omap2430_dss_dispc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_dispc_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'rfbi' class
 * remote frame buffer interface
 */

static struct omap_hwmod_class_sysconfig omap2430_rfbi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_rfbi_hwmod_class = {
	.name = "rfbi",
	.sysc = &omap2430_rfbi_sysc,
};

static struct omap_hwmod_addr_space omap2430_dss_rfbi_addrs[] = {
	{
		.pa_start	= 0x48050800,
		.pa_end		= 0x48050BFF,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> dss_rfbi */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_rfbi = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2430_dss_rfbi_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_dss_rfbi_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_rfbi slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_rfbi_slaves[] = {
	&omap2430_l4_core__dss_rfbi,
};

static struct omap_hwmod omap2430_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2430_rfbi_hwmod_class,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.slaves		= omap2430_dss_rfbi_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_rfbi_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'venc' class
 * video encoder
 */

static struct omap_hwmod_class omap2430_venc_hwmod_class = {
	.name = "venc",
};

/* dss_venc */
static struct omap_hwmod_addr_space omap2430_dss_venc_addrs[] = {
	{
		.pa_start	= 0x48050C00,
		.pa_end		= 0x48050FFF,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> dss_venc */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_venc = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_venc_hwmod,
	.clk		= "dss_54m_fck",
	.addr		= omap2430_dss_venc_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_dss_venc_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_venc slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_venc_slaves[] = {
	&omap2430_l4_core__dss_venc,
};

static struct omap_hwmod omap2430_dss_venc_hwmod = {
	.name		= "dss_venc",
	.class		= &omap2430_venc_hwmod_class,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.slaves		= omap2430_dss_venc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_venc_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name		= "i2c",
	.sysc		= &i2c_sysc,
};

static struct omap_i2c_dev_attr i2c_dev_attr = {
	.fifo_depth	= 8, /* bytes */
};

/* I2C1 */

static struct omap_hwmod_irq_info i2c1_mpu_irqs[] = {
	{ .irq = INT_24XX_I2C1_IRQ, },
};

static struct omap_hwmod_dma_info i2c1_sdma_reqs[] = {
	{ .name = "tx", .dma_req = OMAP24XX_DMA_I2C1_TX },
	{ .name = "rx", .dma_req = OMAP24XX_DMA_I2C1_RX },
};

static struct omap_hwmod_ocp_if *omap2430_i2c1_slaves[] = {
	&omap2430_l4_core__i2c1,
};

static struct omap_hwmod omap2430_i2c1_hwmod = {
	.name		= "i2c1",
	.mpu_irqs	= i2c1_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(i2c1_mpu_irqs),
	.sdma_reqs	= i2c1_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(i2c1_sdma_reqs),
	.main_clk	= "i2chs1_fck",
	.prcm		= {
		.omap2 = {
			/*
			 * NOTE: The CM_FCLKEN* and CM_ICLKEN* for
			 * I2CHS IP's do not follow the usual pattern.
			 * prcm_reg_id alone cannot be used to program
			 * the iclk and fclk. Needs to be handled using
			 * additonal flags when clk handling is moved
			 * to hwmod framework.
			 */
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS1_SHIFT,
		},
	},
	.slaves		= omap2430_i2c1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_i2c1_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* I2C2 */

static struct omap_hwmod_irq_info i2c2_mpu_irqs[] = {
	{ .irq = INT_24XX_I2C2_IRQ, },
};

static struct omap_hwmod_dma_info i2c2_sdma_reqs[] = {
	{ .name = "tx", .dma_req = OMAP24XX_DMA_I2C2_TX },
	{ .name = "rx", .dma_req = OMAP24XX_DMA_I2C2_RX },
};

static struct omap_hwmod_ocp_if *omap2430_i2c2_slaves[] = {
	&omap2430_l4_core__i2c2,
};

static struct omap_hwmod omap2430_i2c2_hwmod = {
	.name		= "i2c2",
	.mpu_irqs	= i2c2_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(i2c2_mpu_irqs),
	.sdma_reqs	= i2c2_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(i2c2_sdma_reqs),
	.main_clk	= "i2chs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS2_SHIFT,
		},
	},
	.slaves		= omap2430_i2c2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_i2c2_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_addr_space omap2430_gpio1_addr_space[] = {
	{
		.pa_start	= 0x4900C000,
		.pa_end		= 0x4900C1ff,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio1 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio1_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio1_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_gpio1_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio2 */
static struct omap_hwmod_addr_space omap2430_gpio2_addr_space[] = {
	{
		.pa_start	= 0x4900E000,
		.pa_end		= 0x4900E1ff,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio2 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio2_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio2_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_gpio2_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio3 */
static struct omap_hwmod_addr_space omap2430_gpio3_addr_space[] = {
	{
		.pa_start	= 0x49010000,
		.pa_end		= 0x490101ff,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio3 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio3_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio3_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_gpio3_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio4 */
static struct omap_hwmod_addr_space omap2430_gpio4_addr_space[] = {
	{
		.pa_start	= 0x49012000,
		.pa_end		= 0x490121ff,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio4 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio4_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio4_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_gpio4_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> gpio5 */
static struct omap_hwmod_addr_space omap2430_gpio5_addr_space[] = {
	{
		.pa_start	= 0x480B6000,
		.pa_end		= 0x480B61ff,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_core__gpio5 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_gpio5_hwmod,
	.clk		= "gpio5_ick",
	.addr		= omap2430_gpio5_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap2430_gpio5_addr_space),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* gpio dev_attr */
static struct omap_gpio_dev_attr gpio_dev_attr = {
	.bank_width = 32,
	.dbck_flag = false,
};

static struct omap_hwmod_class_sysconfig omap243x_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

/*
 * 'gpio' class
 * general purpose io module
 */
static struct omap_hwmod_class omap243x_gpio_hwmod_class = {
	.name = "gpio",
	.sysc = &omap243x_gpio_sysc,
	.rev = 0,
};

/* gpio1 */
static struct omap_hwmod_irq_info omap243x_gpio1_irqs[] = {
	{ .irq = 29 }, /* INT_24XX_GPIO_BANK1 */
};

static struct omap_hwmod_ocp_if *omap2430_gpio1_slaves[] = {
	&omap2430_l4_wkup__gpio1,
};

static struct omap_hwmod omap2430_gpio1_hwmod = {
	.name		= "gpio1",
	.mpu_irqs	= omap243x_gpio1_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap243x_gpio1_irqs),
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio1_slaves),
	.class		= &omap243x_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* gpio2 */
static struct omap_hwmod_irq_info omap243x_gpio2_irqs[] = {
	{ .irq = 30 }, /* INT_24XX_GPIO_BANK2 */
};

static struct omap_hwmod_ocp_if *omap2430_gpio2_slaves[] = {
	&omap2430_l4_wkup__gpio2,
};

static struct omap_hwmod omap2430_gpio2_hwmod = {
	.name		= "gpio2",
	.mpu_irqs	= omap243x_gpio2_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap243x_gpio2_irqs),
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio2_slaves),
	.class		= &omap243x_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* gpio3 */
static struct omap_hwmod_irq_info omap243x_gpio3_irqs[] = {
	{ .irq = 31 }, /* INT_24XX_GPIO_BANK3 */
};

static struct omap_hwmod_ocp_if *omap2430_gpio3_slaves[] = {
	&omap2430_l4_wkup__gpio3,
};

static struct omap_hwmod omap2430_gpio3_hwmod = {
	.name		= "gpio3",
	.mpu_irqs	= omap243x_gpio3_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap243x_gpio3_irqs),
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio3_slaves),
	.class		= &omap243x_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* gpio4 */
static struct omap_hwmod_irq_info omap243x_gpio4_irqs[] = {
	{ .irq = 32 }, /* INT_24XX_GPIO_BANK4 */
};

static struct omap_hwmod_ocp_if *omap2430_gpio4_slaves[] = {
	&omap2430_l4_wkup__gpio4,
};

static struct omap_hwmod omap2430_gpio4_hwmod = {
	.name		= "gpio4",
	.mpu_irqs	= omap243x_gpio4_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap243x_gpio4_irqs),
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio4_slaves),
	.class		= &omap243x_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* gpio5 */
static struct omap_hwmod_irq_info omap243x_gpio5_irqs[] = {
	{ .irq = 33 }, /* INT_24XX_GPIO_BANK5 */
};

static struct omap_hwmod_ocp_if *omap2430_gpio5_slaves[] = {
	&omap2430_l4_core__gpio5,
};

static struct omap_hwmod omap2430_gpio5_hwmod = {
	.name		= "gpio5",
	.mpu_irqs	= omap243x_gpio5_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap243x_gpio5_irqs),
	.main_clk	= "gpio5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_GPIO5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_GPIO5_SHIFT,
		},
	},
	.slaves		= omap2430_gpio5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio5_slaves),
	.class		= &omap243x_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* dma_system */
static struct omap_hwmod_class_sysconfig omap2430_dma_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x002c,
	.syss_offs	= 0x0028,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_EMUFREE |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_dma_hwmod_class = {
	.name = "dma",
	.sysc = &omap2430_dma_sysc,
};

/* dma attributes */
static struct omap_dma_dev_attr dma_dev_attr = {
	.dev_caps  = RESERVE_CHANNEL | DMA_LINKED_LCH | GLOBAL_PRIORITY |
				IS_CSSA_32 | IS_CDSA_32 | IS_RW_PRIORITY,
	.lch_count = 32,
};

static struct omap_hwmod_irq_info omap2430_dma_system_irqs[] = {
	{ .name = "0", .irq = 12 }, /* INT_24XX_SDMA_IRQ0 */
	{ .name = "1", .irq = 13 }, /* INT_24XX_SDMA_IRQ1 */
	{ .name = "2", .irq = 14 }, /* INT_24XX_SDMA_IRQ2 */
	{ .name = "3", .irq = 15 }, /* INT_24XX_SDMA_IRQ3 */
};

static struct omap_hwmod_addr_space omap2430_dma_system_addrs[] = {
	{
		.pa_start	= 0x48056000,
		.pa_end		= 0x4a0560ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap2430_dma_system__l3 = {
	.master		= &omap2430_dma_system_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system master ports */
static struct omap_hwmod_ocp_if *omap2430_dma_system_masters[] = {
	&omap2430_dma_system__l3,
};

/* l4_core -> dma_system */
static struct omap_hwmod_ocp_if omap2430_l4_core__dma_system = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dma_system_hwmod,
	.clk		= "sdma_ick",
	.addr		= omap2430_dma_system_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_dma_system_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system slave ports */
static struct omap_hwmod_ocp_if *omap2430_dma_system_slaves[] = {
	&omap2430_l4_core__dma_system,
};

static struct omap_hwmod omap2430_dma_system_hwmod = {
	.name		= "dma",
	.class		= &omap2430_dma_hwmod_class,
	.mpu_irqs	= omap2430_dma_system_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_dma_system_irqs),
	.main_clk	= "core_l3_ck",
	.slaves		= omap2430_dma_system_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dma_system_slaves),
	.masters	= omap2430_dma_system_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_dma_system_masters),
	.dev_attr	= &dma_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors
 * using a queued mailbox-interrupt mechanism.
 */

static struct omap_hwmod_class_sysconfig omap2430_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mailbox_hwmod_class = {
	.name = "mailbox",
	.sysc = &omap2430_mailbox_sysc,
};

/* mailbox */
static struct omap_hwmod omap2430_mailbox_hwmod;
static struct omap_hwmod_irq_info omap2430_mailbox_irqs[] = {
	{ .irq = 26 },
};

static struct omap_hwmod_addr_space omap2430_mailbox_addrs[] = {
	{
		.pa_start	= 0x48094000,
		.pa_end		= 0x480941ff,
		.flags		= ADDR_TYPE_RT,
	},
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap2430_l4_core__mailbox = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mailbox_hwmod,
	.addr		= omap2430_mailbox_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mailbox_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mailbox slave ports */
static struct omap_hwmod_ocp_if *omap2430_mailbox_slaves[] = {
	&omap2430_l4_core__mailbox,
};

static struct omap_hwmod omap2430_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap2430_mailbox_hwmod_class,
	.mpu_irqs	= omap2430_mailbox_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mailbox_irqs),
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MAILBOXES_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MAILBOXES_SHIFT,
		},
	},
	.slaves		= omap2430_mailbox_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mailbox_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/*
 * 'mcspi' class
 * multichannel serial port interface (mcspi) / master/slave synchronous serial
 * bus
 */

static struct omap_hwmod_class_sysconfig omap2430_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mcspi_class = {
	.name = "mcspi",
	.sysc = &omap2430_mcspi_sysc,
	.rev = OMAP2_MCSPI_REV,
};

/* mcspi1 */
static struct omap_hwmod_irq_info omap2430_mcspi1_mpu_irqs[] = {
	{ .irq = 65 },
};

static struct omap_hwmod_dma_info omap2430_mcspi1_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 35 }, /* DMA_SPI1_TX0 */
	{ .name = "rx0", .dma_req = 36 }, /* DMA_SPI1_RX0 */
	{ .name = "tx1", .dma_req = 37 }, /* DMA_SPI1_TX1 */
	{ .name = "rx1", .dma_req = 38 }, /* DMA_SPI1_RX1 */
	{ .name = "tx2", .dma_req = 39 }, /* DMA_SPI1_TX2 */
	{ .name = "rx2", .dma_req = 40 }, /* DMA_SPI1_RX2 */
	{ .name = "tx3", .dma_req = 41 }, /* DMA_SPI1_TX3 */
	{ .name = "rx3", .dma_req = 42 }, /* DMA_SPI1_RX3 */
};

static struct omap_hwmod_ocp_if *omap2430_mcspi1_slaves[] = {
	&omap2430_l4_core__mcspi1,
};

static struct omap2_mcspi_dev_attr omap_mcspi1_dev_attr = {
	.num_chipselect = 4,
};

static struct omap_hwmod omap2430_mcspi1_hwmod = {
	.name		= "mcspi1_hwmod",
	.mpu_irqs	= omap2430_mcspi1_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcspi1_mpu_irqs),
	.sdma_reqs	= omap2430_mcspi1_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcspi1_sdma_reqs),
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI1_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi1_slaves),
	.class		= &omap2430_mcspi_class,
	.dev_attr       = &omap_mcspi1_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcspi2 */
static struct omap_hwmod_irq_info omap2430_mcspi2_mpu_irqs[] = {
	{ .irq = 66 },
};

static struct omap_hwmod_dma_info omap2430_mcspi2_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 43 }, /* DMA_SPI2_TX0 */
	{ .name = "rx0", .dma_req = 44 }, /* DMA_SPI2_RX0 */
	{ .name = "tx1", .dma_req = 45 }, /* DMA_SPI2_TX1 */
	{ .name = "rx1", .dma_req = 46 }, /* DMA_SPI2_RX1 */
};

static struct omap_hwmod_ocp_if *omap2430_mcspi2_slaves[] = {
	&omap2430_l4_core__mcspi2,
};

static struct omap2_mcspi_dev_attr omap_mcspi2_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap2430_mcspi2_hwmod = {
	.name		= "mcspi2_hwmod",
	.mpu_irqs	= omap2430_mcspi2_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcspi2_mpu_irqs),
	.sdma_reqs	= omap2430_mcspi2_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcspi2_sdma_reqs),
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI2_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi2_slaves),
	.class		= &omap2430_mcspi_class,
	.dev_attr       = &omap_mcspi2_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcspi3 */
static struct omap_hwmod_irq_info omap2430_mcspi3_mpu_irqs[] = {
	{ .irq = 91 },
};

static struct omap_hwmod_dma_info omap2430_mcspi3_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 15 }, /* DMA_SPI3_TX0 */
	{ .name = "rx0", .dma_req = 16 }, /* DMA_SPI3_RX0 */
	{ .name = "tx1", .dma_req = 23 }, /* DMA_SPI3_TX1 */
	{ .name = "rx1", .dma_req = 24 }, /* DMA_SPI3_RX1 */
};

static struct omap_hwmod_ocp_if *omap2430_mcspi3_slaves[] = {
	&omap2430_l4_core__mcspi3,
};

static struct omap2_mcspi_dev_attr omap_mcspi3_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap2430_mcspi3_hwmod = {
	.name		= "mcspi3_hwmod",
	.mpu_irqs	= omap2430_mcspi3_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcspi3_mpu_irqs),
	.sdma_reqs	= omap2430_mcspi3_sdma_reqs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcspi3_sdma_reqs),
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_MCSPI3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCSPI3_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi3_slaves),
	.class		= &omap2430_mcspi_class,
	.dev_attr       = &omap_mcspi3_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/*
 * usbhsotg
 */
static struct omap_hwmod_class_sysconfig omap2430_usbhsotg_sysc = {
	.rev_offs	= 0x0400,
	.sysc_offs	= 0x0404,
	.syss_offs	= 0x0408,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE|
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class usbotg_class = {
	.name = "usbotg",
	.sysc = &omap2430_usbhsotg_sysc,
};

/* usb_otg_hs */
static struct omap_hwmod_irq_info omap2430_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 92 },
	{ .name = "dma", .irq = 93 },
};

static struct omap_hwmod omap2430_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.mpu_irqs	= omap2430_usbhsotg_mpu_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_usbhsotg_mpu_irqs),
	.main_clk	= "usbhs_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_USBHS_MASK,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_USBHS_SHIFT,
		},
	},
	.masters	= omap2430_usbhsotg_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_usbhsotg_masters),
	.slaves		= omap2430_usbhsotg_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_usbhsotg_slaves),
	.class		= &usbotg_class,
	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_SWSUP_MSTANDBY,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430)
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap2430_mcbsp_sysc = {
	.rev_offs	= 0x007C,
	.sysc_offs	= 0x008C,
	.sysc_flags	= (SYSC_HAS_SOFTRESET),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap2430_mcbsp_sysc,
	.rev  = MCBSP_CONFIG_TYPE2,
};

/* mcbsp1 */
static struct omap_hwmod_irq_info omap2430_mcbsp1_irqs[] = {
	{ .name = "tx",		.irq = 59 },
	{ .name = "rx",		.irq = 60 },
	{ .name = "ovr",	.irq = 61 },
	{ .name = "common",	.irq = 64 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp1_sdma_chs[] = {
	{ .name = "rx", .dma_req = 32 },
	{ .name = "tx", .dma_req = 31 },
};

static struct omap_hwmod_addr_space omap2430_mcbsp1_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48074000,
		.pa_end		= 0x480740ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.addr		= omap2430_mcbsp1_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcbsp1_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp1 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp1_slaves[] = {
	&omap2430_l4_core__mcbsp1,
};

static struct omap_hwmod omap2430_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp1_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcbsp1_irqs),
	.sdma_reqs	= omap2430_mcbsp1_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcbsp1_sdma_chs),
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP1_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp1_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcbsp2 */
static struct omap_hwmod_irq_info omap2430_mcbsp2_irqs[] = {
	{ .name = "tx",		.irq = 62 },
	{ .name = "rx",		.irq = 63 },
	{ .name = "common",	.irq = 16 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp2_sdma_chs[] = {
	{ .name = "rx", .dma_req = 34 },
	{ .name = "tx", .dma_req = 33 },
};

static struct omap_hwmod_addr_space omap2430_mcbsp2_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48076000,
		.pa_end		= 0x480760ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> mcbsp2 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap2430_mcbsp2_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcbsp2_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp2 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp2_slaves[] = {
	&omap2430_l4_core__mcbsp2,
};

static struct omap_hwmod omap2430_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp2_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcbsp2_irqs),
	.sdma_reqs	= omap2430_mcbsp2_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcbsp2_sdma_chs),
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP2_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP2_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp2_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcbsp3 */
static struct omap_hwmod_irq_info omap2430_mcbsp3_irqs[] = {
	{ .name = "tx",		.irq = 89 },
	{ .name = "rx",		.irq = 90 },
	{ .name = "common",	.irq = 17 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp3_sdma_chs[] = {
	{ .name = "rx", .dma_req = 18 },
	{ .name = "tx", .dma_req = 17 },
};

static struct omap_hwmod_addr_space omap2430_mcbsp3_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808C000,
		.pa_end		= 0x4808C0ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> mcbsp3 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap2430_mcbsp3_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcbsp3_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp3 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp3_slaves[] = {
	&omap2430_l4_core__mcbsp3,
};

static struct omap_hwmod omap2430_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp3_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcbsp3_irqs),
	.sdma_reqs	= omap2430_mcbsp3_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcbsp3_sdma_chs),
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP3_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP3_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp3_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcbsp4 */
static struct omap_hwmod_irq_info omap2430_mcbsp4_irqs[] = {
	{ .name = "tx",		.irq = 54 },
	{ .name = "rx",		.irq = 55 },
	{ .name = "common",	.irq = 18 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp4_sdma_chs[] = {
	{ .name = "rx", .dma_req = 20 },
	{ .name = "tx", .dma_req = 19 },
};

static struct omap_hwmod_addr_space omap2430_mcbsp4_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808E000,
		.pa_end		= 0x4808E0ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> mcbsp4 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp4 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.addr		= omap2430_mcbsp4_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcbsp4_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp4 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp4_slaves[] = {
	&omap2430_l4_core__mcbsp4,
};

static struct omap_hwmod omap2430_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp4_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcbsp4_irqs),
	.sdma_reqs	= omap2430_mcbsp4_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcbsp4_sdma_chs),
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP4_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP4_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp4_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* mcbsp5 */
static struct omap_hwmod_irq_info omap2430_mcbsp5_irqs[] = {
	{ .name = "tx",		.irq = 81 },
	{ .name = "rx",		.irq = 82 },
	{ .name = "common",	.irq = 19 },
};

static struct omap_hwmod_dma_info omap2430_mcbsp5_sdma_chs[] = {
	{ .name = "rx", .dma_req = 22 },
	{ .name = "tx", .dma_req = 21 },
};

static struct omap_hwmod_addr_space omap2430_mcbsp5_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48096000,
		.pa_end		= 0x480960ff,
		.flags		= ADDR_TYPE_RT
	},
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp5 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.addr		= omap2430_mcbsp5_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_mcbsp5_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp5 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp5_slaves[] = {
	&omap2430_l4_core__mcbsp5,
};

static struct omap_hwmod omap2430_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp5_irqs,
	.mpu_irqs_cnt	= ARRAY_SIZE(omap2430_mcbsp5_irqs),
	.sdma_reqs	= omap2430_mcbsp5_sdma_chs,
	.sdma_reqs_cnt	= ARRAY_SIZE(omap2430_mcbsp5_sdma_chs),
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP5_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp5_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static __initdata struct omap_hwmod *omap2430_hwmods[] = {
	&omap2430_l3_main_hwmod,
	&omap2430_l4_core_hwmod,
	&omap2430_l4_wkup_hwmod,
	&omap2430_mpu_hwmod,
	&omap2430_iva_hwmod,
	&omap2430_wd_timer2_hwmod,
	&omap2430_uart1_hwmod,
	&omap2430_uart2_hwmod,
	&omap2430_uart3_hwmod,
	/* dss class */
	&omap2430_dss_core_hwmod,
	&omap2430_dss_dispc_hwmod,
	&omap2430_dss_rfbi_hwmod,
	&omap2430_dss_venc_hwmod,
	/* i2c class */
	&omap2430_i2c1_hwmod,
	&omap2430_i2c2_hwmod,

	/* gpio class */
	&omap2430_gpio1_hwmod,
	&omap2430_gpio2_hwmod,
	&omap2430_gpio3_hwmod,
	&omap2430_gpio4_hwmod,
	&omap2430_gpio5_hwmod,

	/* dma_system class*/
	&omap2430_dma_system_hwmod,

	/* mcbsp class */
	&omap2430_mcbsp1_hwmod,
	&omap2430_mcbsp2_hwmod,
	&omap2430_mcbsp3_hwmod,
	&omap2430_mcbsp4_hwmod,
	&omap2430_mcbsp5_hwmod,

	/* mailbox class */
	&omap2430_mailbox_hwmod,

	/* mcspi class */
	&omap2430_mcspi1_hwmod,
	&omap2430_mcspi2_hwmod,
	&omap2430_mcspi3_hwmod,

	/* usbotg class*/
	&omap2430_usbhsotg_hwmod,

	NULL,
};

int __init omap2430_hwmod_init(void)
{
	return omap_hwmod_init(omap2430_hwmods);
}
