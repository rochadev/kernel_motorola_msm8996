/*
 * AMD64 class Memory Controller kernel module
 *
 * Copyright (c) 2009 SoftwareBitMaker.
 * Copyright (c) 2009 Advanced Micro Devices, Inc.
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 *	Originally Written by Thayne Harbaugh
 *
 *      Changes by Douglas "norsk" Thompson  <dougthompson@xmission.com>:
 *		- K8 CPU Revision D and greater support
 *
 *      Changes by Dave Peterson <dsp@llnl.gov> <dave_peterson@pobox.com>:
 *		- Module largely rewritten, with new (and hopefully correct)
 *		code for dealing with node and chip select interleaving,
 *		various code cleanup, and bug fixes
 *		- Added support for memory hoisting using DRAM hole address
 *		register
 *
 *	Changes by Douglas "norsk" Thompson <dougthompson@xmission.com>:
 *		-K8 Rev (1207) revision support added, required Revision
 *		specific mini-driver code to support Rev F as well as
 *		prior revisions
 *
 *	Changes by Douglas "norsk" Thompson <dougthompson@xmission.com>:
 *		-Family 10h revision support added. New PCI Device IDs,
 *		indicating new changes. Actual registers modified
 *		were slight, less than the Rev E to Rev F transition
 *		but changing the PCI Device ID was the proper thing to
 *		do, as it provides for almost automactic family
 *		detection. The mods to Rev F required more family
 *		information detection.
 *
 *	Changes/Fixes by Borislav Petkov <borislav.petkov@amd.com>:
 *		- misc fixes and code cleanups
 *
 * This module is based on the following documents
 * (available from http://www.amd.com/):
 *
 *	Title:	BIOS and Kernel Developer's Guide for AMD Athlon 64 and AMD
 *		Opteron Processors
 *	AMD publication #: 26094
 *`	Revision: 3.26
 *
 *	Title:	BIOS and Kernel Developer's Guide for AMD NPT Family 0Fh
 *		Processors
 *	AMD publication #: 32559
 *	Revision: 3.00
 *	Issue Date: May 2006
 *
 *	Title:	BIOS and Kernel Developer's Guide (BKDG) For AMD Family 10h
 *		Processors
 *	AMD publication #: 31116
 *	Revision: 3.00
 *	Issue Date: September 07, 2007
 *
 * Sections in the first 2 documents are no longer in sync with each other.
 * The Family 10h BKDG was totally re-written from scratch with a new
 * presentation model.
 * Therefore, comments that refer to a Document section might be off.
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/edac.h>
#include <asm/msr.h>
#include "edac_core.h"
#include "mce_amd.h"

#define amd64_debug(fmt, arg...) \
	edac_printk(KERN_DEBUG, "amd64", fmt, ##arg)

#define amd64_info(fmt, arg...) \
	edac_printk(KERN_INFO, "amd64", fmt, ##arg)

#define amd64_notice(fmt, arg...) \
	edac_printk(KERN_NOTICE, "amd64", fmt, ##arg)

#define amd64_warn(fmt, arg...) \
	edac_printk(KERN_WARNING, "amd64", fmt, ##arg)

#define amd64_err(fmt, arg...) \
	edac_printk(KERN_ERR, "amd64", fmt, ##arg)

#define amd64_mc_warn(mci, fmt, arg...) \
	edac_mc_chipset_printk(mci, KERN_WARNING, "amd64", fmt, ##arg)

#define amd64_mc_err(mci, fmt, arg...) \
	edac_mc_chipset_printk(mci, KERN_ERR, "amd64", fmt, ##arg)

/*
 * Throughout the comments in this code, the following terms are used:
 *
 *	SysAddr, DramAddr, and InputAddr
 *
 *  These terms come directly from the amd64 documentation
 * (AMD publication #26094).  They are defined as follows:
 *
 *     SysAddr:
 *         This is a physical address generated by a CPU core or a device
 *         doing DMA.  If generated by a CPU core, a SysAddr is the result of
 *         a virtual to physical address translation by the CPU core's address
 *         translation mechanism (MMU).
 *
 *     DramAddr:
 *         A DramAddr is derived from a SysAddr by subtracting an offset that
 *         depends on which node the SysAddr maps to and whether the SysAddr
 *         is within a range affected by memory hoisting.  The DRAM Base
 *         (section 3.4.4.1) and DRAM Limit (section 3.4.4.2) registers
 *         determine which node a SysAddr maps to.
 *
 *         If the DRAM Hole Address Register (DHAR) is enabled and the SysAddr
 *         is within the range of addresses specified by this register, then
 *         a value x from the DHAR is subtracted from the SysAddr to produce a
 *         DramAddr.  Here, x represents the base address for the node that
 *         the SysAddr maps to plus an offset due to memory hoisting.  See
 *         section 3.4.8 and the comments in amd64_get_dram_hole_info() and
 *         sys_addr_to_dram_addr() below for more information.
 *
 *         If the SysAddr is not affected by the DHAR then a value y is
 *         subtracted from the SysAddr to produce a DramAddr.  Here, y is the
 *         base address for the node that the SysAddr maps to.  See section
 *         3.4.4 and the comments in sys_addr_to_dram_addr() below for more
 *         information.
 *
 *     InputAddr:
 *         A DramAddr is translated to an InputAddr before being passed to the
 *         memory controller for the node that the DramAddr is associated
 *         with.  The memory controller then maps the InputAddr to a csrow.
 *         If node interleaving is not in use, then the InputAddr has the same
 *         value as the DramAddr.  Otherwise, the InputAddr is produced by
 *         discarding the bits used for node interleaving from the DramAddr.
 *         See section 3.4.4 for more information.
 *
 *         The memory controller for a given node uses its DRAM CS Base and
 *         DRAM CS Mask registers to map an InputAddr to a csrow.  See
 *         sections 3.5.4 and 3.5.5 for more information.
 */

#define EDAC_AMD64_VERSION		"v3.3.0"
#define EDAC_MOD_STR			"amd64_edac"

/* Extended Model from CPUID, for CPU Revision numbers */
#define K8_REV_D			1
#define K8_REV_E			2
#define K8_REV_F			4

/* Hardware limit on ChipSelect rows per MC and processors per system */
#define MAX_CS_COUNT			8
#define DRAM_REG_COUNT			8

#define ON true
#define OFF false

/*
 * PCI-defined configuration space registers
 */


/*
 * Function 1 - Address Map
 */
#define K8_DRAM_BASE_LOW		0x40
#define K8_DRAM_LIMIT_LOW		0x44
#define K8_DHAR				0xf0

#define DHAR_VALID			BIT(0)
#define F10_DRAM_MEM_HOIST_VALID	BIT(1)

#define DHAR_BASE_MASK			0xff000000
#define dhar_base(dhar)			(dhar & DHAR_BASE_MASK)

#define K8_DHAR_OFFSET_MASK		0x0000ff00
#define k8_dhar_offset(dhar)		((dhar & K8_DHAR_OFFSET_MASK) << 16)

#define F10_DHAR_OFFSET_MASK		0x0000ff80
					/* NOTE: Extra mask bit vs K8 */
#define f10_dhar_offset(dhar)		((dhar & F10_DHAR_OFFSET_MASK) << 16)

#define DCT_CFG_SEL			0x10C

/* F10 High BASE/LIMIT registers */
#define F10_DRAM_BASE_HIGH		0x140
#define F10_DRAM_LIMIT_HIGH		0x144


/*
 * Function 2 - DRAM controller
 */
#define K8_DCSB0			0x40
#define F10_DCSB1			0x140

#define K8_DCSB_CS_ENABLE		BIT(0)
#define K8_DCSB_NPT_SPARE		BIT(1)
#define K8_DCSB_NPT_TESTFAIL		BIT(2)

/*
 * REV E: select [31:21] and [15:9] from DCSB and the shift amount to form
 * the address
 */
#define REV_E_DCSB_BASE_BITS		(0xFFE0FE00ULL)
#define REV_E_DCS_SHIFT			4

#define REV_F_F1Xh_DCSB_BASE_BITS	(0x1FF83FE0ULL)
#define REV_F_F1Xh_DCS_SHIFT		8

/*
 * REV F and later: selects [28:19] and [13:5] from DCSB and the shift amount
 * to form the address
 */
#define REV_F_DCSB_BASE_BITS		(0x1FF83FE0ULL)
#define REV_F_DCS_SHIFT			8

/* DRAM CS Mask Registers */
#define K8_DCSM0			0x60
#define F10_DCSM1			0x160

/* REV E: select [29:21] and [15:9] from DCSM */
#define REV_E_DCSM_MASK_BITS		0x3FE0FE00

/* unused bits [24:20] and [12:0] */
#define REV_E_DCS_NOTUSED_BITS		0x01F01FFF

/* REV F and later: select [28:19] and [13:5] from DCSM */
#define REV_F_F1Xh_DCSM_MASK_BITS	0x1FF83FE0

/* unused bits [26:22] and [12:0] */
#define REV_F_F1Xh_DCS_NOTUSED_BITS	0x07C01FFF

#define DBAM0				0x80
#define DBAM1				0x180

/* Extract the DIMM 'type' on the i'th DIMM from the DBAM reg value passed */
#define DBAM_DIMM(i, reg)		((((reg) >> (4*i))) & 0xF)

#define DBAM_MAX_VALUE			11


#define F10_DCLR_0			0x90
#define F10_DCLR_1			0x190
#define REVE_WIDTH_128			BIT(16)
#define F10_WIDTH_128			BIT(11)


#define F10_DCHR_0			0x94
#define F10_DCHR_1			0x194

#define F10_DCHR_FOUR_RANK_DIMM		BIT(18)
#define DDR3_MODE			BIT(8)
#define F10_DCHR_MblMode		BIT(6)


#define F10_DCTL_SEL_LOW		0x110
#define dct_sel_baseaddr(pvt)		((pvt->dct_sel_low) & 0xFFFFF800)
#define dct_sel_interleave_addr(pvt)	(((pvt->dct_sel_low) >> 6) & 0x3)
#define dct_high_range_enabled(pvt)	(pvt->dct_sel_low & BIT(0))
#define dct_interleave_enabled(pvt)	(pvt->dct_sel_low & BIT(2))
#define dct_ganging_enabled(pvt)	(pvt->dct_sel_low & BIT(4))
#define dct_data_intlv_enabled(pvt)	(pvt->dct_sel_low & BIT(5))
#define dct_dram_enabled(pvt)		(pvt->dct_sel_low & BIT(8))
#define dct_memory_cleared(pvt)		(pvt->dct_sel_low & BIT(10))

#define F10_DCTL_SEL_HIGH		0x114

/*
 * Function 3 - Misc Control
 */
#define K8_NBCTL			0x40

/* Correctable ECC error reporting enable */
#define K8_NBCTL_CECCEn			BIT(0)

/* UnCorrectable ECC error reporting enable */
#define K8_NBCTL_UECCEn			BIT(1)

#define K8_NBCFG			0x44
#define K8_NBCFG_CHIPKILL		BIT(23)
#define K8_NBCFG_ECC_ENABLE		BIT(22)

#define K8_NBSL				0x48


/* Family F10h: Normalized Extended Error Codes */
#define F10_NBSL_EXT_ERR_RES		0x0
#define F10_NBSL_EXT_ERR_ECC		0x8

/* Next two are overloaded values */
#define F10_NBSL_EXT_ERR_LINK_PROTO	0xB
#define F10_NBSL_EXT_ERR_L3_PROTO	0xB

#define F10_NBSL_EXT_ERR_NB_ARRAY	0xC
#define F10_NBSL_EXT_ERR_DRAM_PARITY	0xD
#define F10_NBSL_EXT_ERR_LINK_RETRY	0xE

/* Next two are overloaded values */
#define F10_NBSL_EXT_ERR_GART_WALK	0xF
#define F10_NBSL_EXT_ERR_DEV_WALK	0xF

/* 0x10 to 0x1B: Reserved */
#define F10_NBSL_EXT_ERR_L3_DATA	0x1C
#define F10_NBSL_EXT_ERR_L3_TAG		0x1D
#define F10_NBSL_EXT_ERR_L3_LRU		0x1E

/* K8: Normalized Extended Error Codes */
#define K8_NBSL_EXT_ERR_ECC		0x0
#define K8_NBSL_EXT_ERR_CRC		0x1
#define K8_NBSL_EXT_ERR_SYNC		0x2
#define K8_NBSL_EXT_ERR_MST		0x3
#define K8_NBSL_EXT_ERR_TGT		0x4
#define K8_NBSL_EXT_ERR_GART		0x5
#define K8_NBSL_EXT_ERR_RMW		0x6
#define K8_NBSL_EXT_ERR_WDT		0x7
#define K8_NBSL_EXT_ERR_CHIPKILL_ECC	0x8
#define K8_NBSL_EXT_ERR_DRAM_PARITY	0xD

/*
 * The following are for BUS type errors AFTER values have been normalized by
 * shifting right
 */
#define K8_NBSL_PP_SRC			0x0
#define K8_NBSL_PP_RES			0x1
#define K8_NBSL_PP_OBS			0x2
#define K8_NBSL_PP_GENERIC		0x3

#define EXTRACT_ERR_CPU_MAP(x)		((x) & 0xF)

#define K8_NBEAL			0x50
#define K8_NBEAH			0x54
#define K8_SCRCTRL			0x58

#define F10_NB_CFG_LOW			0x88

#define F10_ONLINE_SPARE		0xB0
#define F10_ONLINE_SPARE_SWAPDONE0(x)	((x) & BIT(1))
#define F10_ONLINE_SPARE_SWAPDONE1(x)	((x) & BIT(3))
#define F10_ONLINE_SPARE_BADDRAM_CS0(x) (((x) >> 4) & 0x00000007)
#define F10_ONLINE_SPARE_BADDRAM_CS1(x) (((x) >> 8) & 0x00000007)

#define F10_NB_ARRAY_ADDR		0xB8

#define F10_NB_ARRAY_DRAM_ECC		0x80000000

/* Bits [2:1] are used to select 16-byte section within a 64-byte cacheline  */
#define SET_NB_ARRAY_ADDRESS(section)	(((section) & 0x3) << 1)

#define F10_NB_ARRAY_DATA		0xBC

#define SET_NB_DRAM_INJECTION_WRITE(word, bits)  \
					(BIT(((word) & 0xF) + 20) | \
					BIT(17) | bits)

#define SET_NB_DRAM_INJECTION_READ(word, bits)  \
					(BIT(((word) & 0xF) + 20) | \
					BIT(16) |  bits)

#define K8_NBCAP			0xE8
#define K8_NBCAP_CORES			(BIT(12)|BIT(13))
#define K8_NBCAP_CHIPKILL		BIT(4)
#define K8_NBCAP_SECDED			BIT(3)
#define K8_NBCAP_DCT_DUAL		BIT(0)

#define EXT_NB_MCA_CFG			0x180

/* MSRs */
#define K8_MSR_MCGCTL_NBE		BIT(4)

#define K8_MSR_MC4CTL			0x0410
#define K8_MSR_MC4STAT			0x0411
#define K8_MSR_MC4ADDR			0x0412

/* AMD sets the first MC device at device ID 0x18. */
static inline int get_node_id(struct pci_dev *pdev)
{
	return PCI_SLOT(pdev->devfn) - 0x18;
}

enum amd_families {
	K8_CPUS = 0,
	F10_CPUS,
	F15_CPUS,
	NUM_FAMILIES,
};

/* Error injection control structure */
struct error_injection {
	u32	section;
	u32	word;
	u32	bit_map;
};

struct amd64_pvt {
	struct low_ops *ops;

	/* pci_device handles which we utilize */
	struct pci_dev *F1, *F2, *F3;

	int mc_node_id;		/* MC index of this MC node */
	int ext_model;		/* extended model value of this node */
	int channel_count;

	/* Raw registers */
	u32 dclr0;		/* DRAM Configuration Low DCT0 reg */
	u32 dclr1;		/* DRAM Configuration Low DCT1 reg */
	u32 dchr0;		/* DRAM Configuration High DCT0 reg */
	u32 dchr1;		/* DRAM Configuration High DCT1 reg */
	u32 nbcap;		/* North Bridge Capabilities */
	u32 nbcfg;		/* F10 North Bridge Configuration */
	u32 ext_nbcfg;		/* Extended F10 North Bridge Configuration */
	u32 dhar;		/* DRAM Hoist reg */
	u32 dbam0;		/* DRAM Base Address Mapping reg for DCT0 */
	u32 dbam1;		/* DRAM Base Address Mapping reg for DCT1 */

	/* DRAM CS Base Address Registers F2x[1,0][5C:40] */
	u32 dcsb0[MAX_CS_COUNT];
	u32 dcsb1[MAX_CS_COUNT];

	/* DRAM CS Mask Registers F2x[1,0][6C:60] */
	u32 dcsm0[MAX_CS_COUNT];
	u32 dcsm1[MAX_CS_COUNT];

	/*
	 * Decoded parts of DRAM BASE and LIMIT Registers
	 * F1x[78,70,68,60,58,50,48,40]
	 */
	u64 dram_base[DRAM_REG_COUNT];
	u64 dram_limit[DRAM_REG_COUNT];
	u8  dram_IntlvSel[DRAM_REG_COUNT];
	u8  dram_IntlvEn[DRAM_REG_COUNT];
	u8  dram_DstNode[DRAM_REG_COUNT];
	u8  dram_rw_en[DRAM_REG_COUNT];

	/*
	 * The following fields are set at (load) run time, after CPU revision
	 * has been determined, since the dct_base and dct_mask registers vary
	 * based on revision
	 */
	u32 dcsb_base;		/* DCSB base bits */
	u32 dcsm_mask;		/* DCSM mask bits */
	u32 cs_count;		/* num chip selects (== num DCSB registers) */
	u32 num_dcsm;		/* Number of DCSM registers */
	u32 dcs_mask_notused;	/* DCSM notused mask bits */
	u32 dcs_shift;		/* DCSB and DCSM shift value */

	u64 top_mem;		/* top of memory below 4GB */
	u64 top_mem2;		/* top of memory above 4GB */

	u32 dct_sel_low;	/* DRAM Controller Select Low Reg */
	u32 dct_sel_hi;		/* DRAM Controller Select High Reg */
	u32 online_spare;	/* On-Line spare Reg */

	/* x4 or x8 syndromes in use */
	u8 syn_type;

	/* temp storage for when input is received from sysfs */
	struct err_regs ctl_error_info;

	/* place to store error injection parameters prior to issue */
	struct error_injection injection;

	/* DCT per-family scrubrate setting */
	u32 min_scrubrate;

	/* family name this instance is running on */
	const char *ctl_name;

};

/*
 * per-node ECC settings descriptor
 */
struct ecc_settings {
	u32 old_nbctl;
	bool nbctl_valid;

	struct flags {
		unsigned long nb_mce_enable:1;
		unsigned long nb_ecc_prev:1;
	} flags;
};

extern const char *tt_msgs[4];
extern const char *ll_msgs[4];
extern const char *rrrr_msgs[16];
extern const char *to_msgs[2];
extern const char *pp_msgs[4];
extern const char *ii_msgs[4];
extern const char *htlink_msgs[8];

#ifdef CONFIG_EDAC_DEBUG
#define NUM_DBG_ATTRS 5
#else
#define NUM_DBG_ATTRS 0
#endif

#ifdef CONFIG_EDAC_AMD64_ERROR_INJECTION
#define NUM_INJ_ATTRS 5
#else
#define NUM_INJ_ATTRS 0
#endif

extern struct mcidev_sysfs_attribute amd64_dbg_attrs[NUM_DBG_ATTRS],
				     amd64_inj_attrs[NUM_INJ_ATTRS];

/*
 * Each of the PCI Device IDs types have their own set of hardware accessor
 * functions and per device encoding/decoding logic.
 */
struct low_ops {
	int (*early_channel_count)	(struct amd64_pvt *pvt);

	u64 (*get_error_address)	(struct mem_ctl_info *mci,
					 struct err_regs *info);
	void (*read_dram_base_limit)	(struct amd64_pvt *pvt, int dram);
	void (*read_dram_ctl_register)	(struct amd64_pvt *pvt);
	void (*map_sysaddr_to_csrow)	(struct mem_ctl_info *mci,
					 struct err_regs *info, u64 SystemAddr);
	int (*dbam_to_cs)		(struct amd64_pvt *pvt, int cs_mode);
	int (*read_dct_pci_cfg)		(struct amd64_pvt *pvt, int offset,
					 u32 *val, const char *func);
};

struct amd64_family_type {
	const char *ctl_name;
	u16 f1_id, f3_id;
	struct low_ops ops;
};

int __amd64_write_pci_cfg_dword(struct pci_dev *pdev, int offset,
				u32 val, const char *func);

#define amd64_read_pci_cfg(pdev, offset, val)	\
	__amd64_read_pci_cfg_dword(pdev, offset, val, __func__)

#define amd64_write_pci_cfg(pdev, offset, val)	\
	__amd64_write_pci_cfg_dword(pdev, offset, val, __func__)

#define amd64_read_dct_pci_cfg(pvt, offset, val) \
	pvt->ops->read_dct_pci_cfg(pvt, offset, val, __func__)

/*
 * For future CPU versions, verify the following as new 'slow' rates appear and
 * modify the necessary skip values for the supported CPU.
 */
#define K8_MIN_SCRUB_RATE_BITS	0x0
#define F10_MIN_SCRUB_RATE_BITS	0x5

int amd64_get_dram_hole_info(struct mem_ctl_info *mci, u64 *hole_base,
			     u64 *hole_offset, u64 *hole_size);
