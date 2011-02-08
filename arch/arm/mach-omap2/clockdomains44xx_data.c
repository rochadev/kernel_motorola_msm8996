/*
 * OMAP4 Clock domains framework
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Abhijit Pagare (abhijitpagare@ti.com)
 * Benoit Cousson (b-cousson@ti.com)
 *
 * This file is automatically generated from the OMAP hardware databases.
 * We respectfully ask that any modifications to this file be coordinated
 * with the public linux-omap@vger.kernel.org mailing list and the
 * authors above to ensure that the autogeneration scripts are kept
 * up-to-date with the file contents.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * To-Do List
 * -> Populate the Sleep/Wakeup dependencies for the domains
 */

#include <linux/kernel.h>
#include <linux/io.h>

#include "clockdomain.h"
#include "cm1_44xx.h"
#include "cm2_44xx.h"

#include "cm-regbits-44xx.h"
#include "prm44xx.h"
#include "prcm44xx.h"
#include "prcm_mpu44xx.h"


static struct clockdomain l4_cefuse_44xx_clkdm = {
	.name		  = "l4_cefuse_clkdm",
	.pwrdm		  = { .name = "cefuse_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CEFUSE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CEFUSE_CEFUSE_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l4_cfg_44xx_clkdm = {
	.name		  = "l4_cfg_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_L4CFG_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain tesla_44xx_clkdm = {
	.name		  = "tesla_clkdm",
	.pwrdm		  = { .name = "tesla_pwrdm" },
	.prcm_partition	  = OMAP4430_CM1_PARTITION,
	.cm_inst	  = OMAP4430_CM1_TESLA_INST,
	.clkdm_offs	  = OMAP4430_CM1_TESLA_TESLA_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_gfx_44xx_clkdm = {
	.name		  = "l3_gfx_clkdm",
	.pwrdm		  = { .name = "gfx_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_GFX_INST,
	.clkdm_offs	  = OMAP4430_CM2_GFX_GFX_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain ivahd_44xx_clkdm = {
	.name		  = "ivahd_clkdm",
	.pwrdm		  = { .name = "ivahd_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_IVAHD_INST,
	.clkdm_offs	  = OMAP4430_CM2_IVAHD_IVAHD_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l4_secure_44xx_clkdm = {
	.name		  = "l4_secure_clkdm",
	.pwrdm		  = { .name = "l4per_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_L4PER_INST,
	.clkdm_offs	  = OMAP4430_CM2_L4PER_L4SEC_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l4_per_44xx_clkdm = {
	.name		  = "l4_per_clkdm",
	.pwrdm		  = { .name = "l4per_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_L4PER_INST,
	.clkdm_offs	  = OMAP4430_CM2_L4PER_L4PER_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain abe_44xx_clkdm = {
	.name		  = "abe_clkdm",
	.pwrdm		  = { .name = "abe_pwrdm" },
	.prcm_partition	  = OMAP4430_CM1_PARTITION,
	.cm_inst	  = OMAP4430_CM1_ABE_INST,
	.clkdm_offs	  = OMAP4430_CM1_ABE_ABE_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_instr_44xx_clkdm = {
	.name		  = "l3_instr_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_L3INSTR_CDOFFS,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_init_44xx_clkdm = {
	.name		  = "l3_init_clkdm",
	.pwrdm		  = { .name = "l3init_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_L3INIT_INST,
	.clkdm_offs	  = OMAP4430_CM2_L3INIT_L3INIT_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain mpuss_44xx_clkdm = {
	.name		  = "mpuss_clkdm",
	.pwrdm		  = { .name = "mpu_pwrdm" },
	.prcm_partition	  = OMAP4430_CM1_PARTITION,
	.cm_inst	  = OMAP4430_CM1_MPU_INST,
	.clkdm_offs	  = OMAP4430_CM1_MPU_MPU_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain mpu0_44xx_clkdm = {
	.name		  = "mpu0_clkdm",
	.pwrdm		  = { .name = "cpu0_pwrdm" },
	.prcm_partition	  = OMAP4430_PRCM_MPU_PARTITION,
	.cm_inst	  = OMAP4430_PRCM_MPU_CPU0_INST,
	.clkdm_offs	  = OMAP4430_PRCM_MPU_CPU0_CPU0_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain mpu1_44xx_clkdm = {
	.name		  = "mpu1_clkdm",
	.pwrdm		  = { .name = "cpu1_pwrdm" },
	.prcm_partition	  = OMAP4430_PRCM_MPU_PARTITION,
	.cm_inst	  = OMAP4430_PRCM_MPU_CPU1_INST,
	.clkdm_offs	  = OMAP4430_PRCM_MPU_CPU1_CPU1_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_emif_44xx_clkdm = {
	.name		  = "l3_emif_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_MEMIF_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l4_ao_44xx_clkdm = {
	.name		  = "l4_ao_clkdm",
	.pwrdm		  = { .name = "always_on_core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_ALWAYS_ON_INST,
	.clkdm_offs	  = OMAP4430_CM2_ALWAYS_ON_ALWON_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain ducati_44xx_clkdm = {
	.name		  = "ducati_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_DUCATI_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_2_44xx_clkdm = {
	.name		  = "l3_2_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_L3_2_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_1_44xx_clkdm = {
	.name		  = "l3_1_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_L3_1_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_d2d_44xx_clkdm = {
	.name		  = "l3_d2d_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_D2D_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain iss_44xx_clkdm = {
	.name		  = "iss_clkdm",
	.pwrdm		  = { .name = "cam_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CAM_INST,
	.clkdm_offs	  = OMAP4430_CM2_CAM_CAM_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_dss_44xx_clkdm = {
	.name		  = "l3_dss_clkdm",
	.pwrdm		  = { .name = "dss_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_DSS_INST,
	.clkdm_offs	  = OMAP4430_CM2_DSS_DSS_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l4_wkup_44xx_clkdm = {
	.name		  = "l4_wkup_clkdm",
	.pwrdm		  = { .name = "wkup_pwrdm" },
	.prcm_partition	  = OMAP4430_PRM_PARTITION,
	.cm_inst	  = OMAP4430_PRM_WKUP_CM_INST,
	.clkdm_offs	  = OMAP4430_PRM_WKUP_CM_WKUP_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain emu_sys_44xx_clkdm = {
	.name		  = "emu_sys_clkdm",
	.pwrdm		  = { .name = "emu_pwrdm" },
	.prcm_partition	  = OMAP4430_PRM_PARTITION,
	.cm_inst	  = OMAP4430_PRM_EMU_CM_INST,
	.clkdm_offs	  = OMAP4430_PRM_EMU_CM_EMU_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain l3_dma_44xx_clkdm = {
	.name		  = "l3_dma_clkdm",
	.pwrdm		  = { .name = "core_pwrdm" },
	.prcm_partition	  = OMAP4430_CM2_PARTITION,
	.cm_inst	  = OMAP4430_CM2_CORE_INST,
	.clkdm_offs	  = OMAP4430_CM2_CORE_SDMA_CDOFFS,
	.flags		  = CLKDM_CAN_FORCE_WAKEUP | CLKDM_CAN_HWSUP,
	.omap_chip	  = OMAP_CHIP_INIT(CHIP_IS_OMAP4430),
};

static struct clockdomain *clockdomains_omap44xx[] __initdata = {
	&l4_cefuse_44xx_clkdm,
	&l4_cfg_44xx_clkdm,
	&tesla_44xx_clkdm,
	&l3_gfx_44xx_clkdm,
	&ivahd_44xx_clkdm,
	&l4_secure_44xx_clkdm,
	&l4_per_44xx_clkdm,
	&abe_44xx_clkdm,
	&l3_instr_44xx_clkdm,
	&l3_init_44xx_clkdm,
	&mpuss_44xx_clkdm,
	&mpu0_44xx_clkdm,
	&mpu1_44xx_clkdm,
	&l3_emif_44xx_clkdm,
	&l4_ao_44xx_clkdm,
	&ducati_44xx_clkdm,
	&l3_2_44xx_clkdm,
	&l3_1_44xx_clkdm,
	&l3_d2d_44xx_clkdm,
	&iss_44xx_clkdm,
	&l3_dss_44xx_clkdm,
	&l4_wkup_44xx_clkdm,
	&emu_sys_44xx_clkdm,
	&l3_dma_44xx_clkdm,
	NULL,
};

void __init omap44xx_clockdomains_init(void)
{
	clkdm_init(clockdomains_omap44xx, NULL);
}
