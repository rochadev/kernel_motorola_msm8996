/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <asm/mach/flash.h>
#include <mach/mxc_nand.h>
#include <mach/hardware.h>

#define DRIVER_NAME "mxc_nand"

#define nfc_is_v21()		(cpu_is_mx25() || cpu_is_mx35())
#define nfc_is_v1()		(cpu_is_mx31() || cpu_is_mx27() || cpu_is_mx21())

/* Addresses for NFC registers */
#define NFC_V1_V2_BUF_SIZE		(host->regs + 0x00)
#define NFC_V1_V2_BUF_ADDR		(host->regs + 0x04)
#define NFC_V1_V2_FLASH_ADDR		(host->regs + 0x06)
#define NFC_V1_V2_FLASH_CMD		(host->regs + 0x08)
#define NFC_V1_V2_CONFIG		(host->regs + 0x0a)
#define NFC_V1_V2_ECC_STATUS_RESULT	(host->regs + 0x0c)
#define NFC_V1_V2_RSLTMAIN_AREA		(host->regs + 0x0e)
#define NFC_V1_V2_RSLTSPARE_AREA	(host->regs + 0x10)
#define NFC_V1_V2_WRPROT		(host->regs + 0x12)
#define NFC_V1_UNLOCKSTART_BLKADDR	(host->regs + 0x14)
#define NFC_V1_UNLOCKEND_BLKADDR	(host->regs + 0x16)
#define NFC_V21_UNLOCKSTART_BLKADDR	(host->regs + 0x20)
#define NFC_V21_UNLOCKEND_BLKADDR	(host->regs + 0x22)
#define NFC_V1_V2_NF_WRPRST		(host->regs + 0x18)
#define NFC_V1_V2_CONFIG1		(host->regs + 0x1a)
#define NFC_V1_V2_CONFIG2		(host->regs + 0x1c)

#define NFC_V1_V2_CONFIG1_SP_EN		(1 << 2)
#define NFC_V1_V2_CONFIG1_ECC_EN	(1 << 3)
#define NFC_V1_V2_CONFIG1_INT_MSK	(1 << 4)
#define NFC_V1_V2_CONFIG1_BIG		(1 << 5)
#define NFC_V1_V2_CONFIG1_RST		(1 << 6)
#define NFC_V1_V2_CONFIG1_CE		(1 << 7)
#define NFC_V1_V2_CONFIG1_ONE_CYCLE	(1 << 8)

#define NFC_V1_V2_CONFIG2_INT		(1 << 15)

/*
 * Operation modes for the NFC. Valid for v1, v2 and v3
 * type controllers.
 */
#define NFC_CMD				(1 << 0)
#define NFC_ADDR			(1 << 1)
#define NFC_INPUT			(1 << 2)
#define NFC_OUTPUT			(1 << 3)
#define NFC_ID				(1 << 4)
#define NFC_STATUS			(1 << 5)

struct mxc_nand_host {
	struct mtd_info		mtd;
	struct nand_chip	nand;
	struct mtd_partition	*parts;
	struct device		*dev;

	void			*spare0;
	void			*main_area0;

	void __iomem		*base;
	void __iomem		*regs;
	int			status_request;
	struct clk		*clk;
	int			clk_act;
	int			irq;
	int			eccsize;

	wait_queue_head_t	irq_waitq;

	uint8_t			*data_buf;
	unsigned int		buf_start;
	int			spare_len;

	void			(*preset)(struct mtd_info *);
	void			(*send_cmd)(struct mxc_nand_host *, uint16_t, int);
	void			(*send_addr)(struct mxc_nand_host *, uint16_t, int);
	void			(*send_page)(struct mtd_info *, unsigned int);
	void			(*send_read_id)(struct mxc_nand_host *);
	uint16_t		(*get_dev_status)(struct mxc_nand_host *);
	int			(*check_int)(struct mxc_nand_host *);
};

/* OOB placement block for use with hardware ecc generation */
static struct nand_ecclayout nandv1_hw_eccoob_smallpage = {
	.eccbytes = 5,
	.eccpos = {6, 7, 8, 9, 10},
	.oobfree = {{0, 5}, {12, 4}, }
};

static struct nand_ecclayout nandv1_hw_eccoob_largepage = {
	.eccbytes = 20,
	.eccpos = {6, 7, 8, 9, 10, 22, 23, 24, 25, 26,
		   38, 39, 40, 41, 42, 54, 55, 56, 57, 58},
	.oobfree = {{2, 4}, {11, 10}, {27, 10}, {43, 10}, {59, 5}, }
};

/* OOB description for 512 byte pages with 16 byte OOB */
static struct nand_ecclayout nandv2_hw_eccoob_smallpage = {
	.eccbytes = 1 * 9,
	.eccpos = {
		 7,  8,  9, 10, 11, 12, 13, 14, 15
	},
	.oobfree = {
		{.offset = 0, .length = 5}
	}
};

/* OOB description for 2048 byte pages with 64 byte OOB */
static struct nand_ecclayout nandv2_hw_eccoob_largepage = {
	.eccbytes = 4 * 9,
	.eccpos = {
		 7,  8,  9, 10, 11, 12, 13, 14, 15,
		23, 24, 25, 26, 27, 28, 29, 30, 31,
		39, 40, 41, 42, 43, 44, 45, 46, 47,
		55, 56, 57, 58, 59, 60, 61, 62, 63
	},
	.oobfree = {
		{.offset = 2, .length = 4},
		{.offset = 16, .length = 7},
		{.offset = 32, .length = 7},
		{.offset = 48, .length = 7}
	}
};

#ifdef CONFIG_MTD_PARTITIONS
static const char *part_probes[] = { "RedBoot", "cmdlinepart", NULL };
#endif

static irqreturn_t mxc_nfc_irq(int irq, void *dev_id)
{
	struct mxc_nand_host *host = dev_id;

	disable_irq_nosync(irq);

	wake_up(&host->irq_waitq);

	return IRQ_HANDLED;
}

static int check_int_v1_v2(struct mxc_nand_host *host)
{
	uint32_t tmp;

	tmp = readw(NFC_V1_V2_CONFIG2);
	if (!(tmp & NFC_V1_V2_CONFIG2_INT))
		return 0;

	writew(tmp & ~NFC_V1_V2_CONFIG2_INT, NFC_V1_V2_CONFIG2);

	return 1;
}

/* This function polls the NANDFC to wait for the basic operation to
 * complete by checking the INT bit of config2 register.
 */
static void wait_op_done(struct mxc_nand_host *host, int useirq)
{
	int max_retries = 8000;

	if (useirq) {
		if (!host->check_int(host)) {

			enable_irq(host->irq);

			wait_event(host->irq_waitq, host->check_int(host));
		}
	} else {
		while (max_retries-- > 0) {
			if (host->check_int(host))
				break;

			udelay(1);
		}
		if (max_retries < 0)
			DEBUG(MTD_DEBUG_LEVEL0, "%s: INT not set\n",
			      __func__);
	}
}

/* This function issues the specified command to the NAND device and
 * waits for completion. */
static void send_cmd_v1_v2(struct mxc_nand_host *host, uint16_t cmd, int useirq)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_cmd(host, 0x%x, %d)\n", cmd, useirq);

	writew(cmd, NFC_V1_V2_FLASH_CMD);
	writew(NFC_CMD, NFC_V1_V2_CONFIG2);

	if (cpu_is_mx21() && (cmd == NAND_CMD_RESET)) {
		int max_retries = 100;
		/* Reset completion is indicated by NFC_CONFIG2 */
		/* being set to 0 */
		while (max_retries-- > 0) {
			if (readw(NFC_V1_V2_CONFIG2) == 0) {
				break;
			}
			udelay(1);
		}
		if (max_retries < 0)
			DEBUG(MTD_DEBUG_LEVEL0, "%s: RESET failed\n",
			      __func__);
	} else {
		/* Wait for operation to complete */
		wait_op_done(host, useirq);
	}
}

/* This function sends an address (or partial address) to the
 * NAND device. The address is used to select the source/destination for
 * a NAND command. */
static void send_addr_v1_v2(struct mxc_nand_host *host, uint16_t addr, int islast)
{
	DEBUG(MTD_DEBUG_LEVEL3, "send_addr(host, 0x%x %d)\n", addr, islast);

	writew(addr, NFC_V1_V2_FLASH_ADDR);
	writew(NFC_ADDR, NFC_V1_V2_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, islast);
}

static void send_page_v1_v2(struct mtd_info *mtd, unsigned int ops)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	int bufs, i;

	if (nfc_is_v1() && mtd->writesize > 512)
		bufs = 4;
	else
		bufs = 1;

	for (i = 0; i < bufs; i++) {

		/* NANDFC buffer 0 is used for page read/write */
		writew(i, NFC_V1_V2_BUF_ADDR);

		writew(ops, NFC_V1_V2_CONFIG2);

		/* Wait for operation to complete */
		wait_op_done(host, true);
	}
}

/* Request the NANDFC to perform a read of the NAND device ID. */
static void send_read_id_v1_v2(struct mxc_nand_host *host)
{
	struct nand_chip *this = &host->nand;

	/* NANDFC buffer 0 is used for device ID output */
	writew(0x0, NFC_V1_V2_BUF_ADDR);

	writew(NFC_ID, NFC_V1_V2_CONFIG2);

	/* Wait for operation to complete */
	wait_op_done(host, true);

	if (this->options & NAND_BUSWIDTH_16) {
		void __iomem *main_buf = host->main_area0;
		/* compress the ID info */
		writeb(readb(main_buf + 2), main_buf + 1);
		writeb(readb(main_buf + 4), main_buf + 2);
		writeb(readb(main_buf + 6), main_buf + 3);
		writeb(readb(main_buf + 8), main_buf + 4);
		writeb(readb(main_buf + 10), main_buf + 5);
	}
	memcpy(host->data_buf, host->main_area0, 16);
}

/* This function requests the NANDFC to perform a read of the
 * NAND device status and returns the current status. */
static uint16_t get_dev_status_v1_v2(struct mxc_nand_host *host)
{
	void __iomem *main_buf = host->main_area0;
	uint32_t store;
	uint16_t ret;

	writew(0x0, NFC_V1_V2_BUF_ADDR);

	/*
	 * The device status is stored in main_area0. To
	 * prevent corruption of the buffer save the value
	 * and restore it afterwards.
	 */
	store = readl(main_buf);

	writew(NFC_STATUS, NFC_V1_V2_CONFIG2);
	wait_op_done(host, true);

	ret = readw(main_buf);

	writel(store, main_buf);

	return ret;
}

/* This functions is used by upper layer to checks if device is ready */
static int mxc_nand_dev_ready(struct mtd_info *mtd)
{
	/*
	 * NFC handles R/B internally. Therefore, this function
	 * always returns status as ready.
	 */
	return 1;
}

static void mxc_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	/*
	 * If HW ECC is enabled, we turn it on during init. There is
	 * no need to enable again here.
	 */
}

static int mxc_nand_correct_data_v1(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	/*
	 * 1-Bit errors are automatically corrected in HW.  No need for
	 * additional correction.  2-Bit errors cannot be corrected by
	 * HW ECC, so we need to return failure
	 */
	uint16_t ecc_status = readw(NFC_V1_V2_ECC_STATUS_RESULT);

	if (((ecc_status & 0x3) == 2) || ((ecc_status >> 2) == 2)) {
		DEBUG(MTD_DEBUG_LEVEL0,
		      "MXC_NAND: HWECC uncorrectable 2-bit ECC error\n");
		return -1;
	}

	return 0;
}

static int mxc_nand_correct_data_v2_v3(struct mtd_info *mtd, u_char *dat,
				 u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	u32 ecc_stat, err;
	int no_subpages = 1;
	int ret = 0;
	u8 ecc_bit_mask, err_limit;

	ecc_bit_mask = (host->eccsize == 4) ? 0x7 : 0xf;
	err_limit = (host->eccsize == 4) ? 0x4 : 0x8;

	no_subpages = mtd->writesize >> 9;

	ecc_stat = readl(NFC_V1_V2_ECC_STATUS_RESULT);

	do {
		err = ecc_stat & ecc_bit_mask;
		if (err > err_limit) {
			printk(KERN_WARNING "UnCorrectable RS-ECC Error\n");
			return -1;
		} else {
			ret += err;
		}
		ecc_stat >>= 4;
	} while (--no_subpages);

	mtd->ecc_stats.corrected += ret;
	pr_debug("%d Symbol Correctable RS-ECC Error\n", ret);

	return ret;
}

static int mxc_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat,
				  u_char *ecc_code)
{
	return 0;
}

static u_char mxc_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint8_t ret;

	/* Check for status request */
	if (host->status_request)
		return host->get_dev_status(host) & 0xFF;

	ret = *(uint8_t *)(host->data_buf + host->buf_start);
	host->buf_start++;

	return ret;
}

static uint16_t mxc_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint16_t ret;

	ret = *(uint16_t *)(host->data_buf + host->buf_start);
	host->buf_start += 2;

	return ret;
}

/* Write data of length len to buffer buf. The data to be
 * written on NAND Flash is first copied to RAMbuffer. After the Data Input
 * Operation by the NFC, the data is written to NAND Flash */
static void mxc_nand_write_buf(struct mtd_info *mtd,
				const u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(host->data_buf + col, buf, n);

	host->buf_start += n;
}

/* Read the data buffer from the NAND Flash. To read the data from NAND
 * Flash first the data output cycle is initiated by the NFC, which copies
 * the data to RAMbuffer. This data of length len is then copied to buffer buf.
 */
static void mxc_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	u16 col = host->buf_start;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min(n, len);

	memcpy(buf, host->data_buf + col, len);

	host->buf_start += len;
}

/* Used by the upper layer to verify the data in NAND Flash
 * with the data in the buf. */
static int mxc_nand_verify_buf(struct mtd_info *mtd,
				const u_char *buf, int len)
{
	return -EFAULT;
}

/* This function is used by upper layer for select and
 * deselect of the NAND chip */
static void mxc_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	switch (chip) {
	case -1:
		/* Disable the NFC clock */
		if (host->clk_act) {
			clk_disable(host->clk);
			host->clk_act = 0;
		}
		break;
	case 0:
		/* Enable the NFC clock */
		if (!host->clk_act) {
			clk_enable(host->clk);
			host->clk_act = 1;
		}
		break;

	default:
		break;
	}
}

/*
 * Function to transfer data to/from spare area.
 */
static void copy_spare(struct mtd_info *mtd, bool bfrom)
{
	struct nand_chip *this = mtd->priv;
	struct mxc_nand_host *host = this->priv;
	u16 i, j;
	u16 n = mtd->writesize >> 9;
	u8 *d = host->data_buf + mtd->writesize;
	u8 *s = host->spare0;
	u16 t = host->spare_len;

	j = (mtd->oobsize / n >> 1) << 1;

	if (bfrom) {
		for (i = 0; i < n - 1; i++)
			memcpy(d + i * j, s + i * t, j);

		/* the last section */
		memcpy(d + i * j, s + i * t, mtd->oobsize - i * j);
	} else {
		for (i = 0; i < n - 1; i++)
			memcpy(&s[i * t], &d[i * j], j);

		/* the last section */
		memcpy(&s[i * t], &d[i * j], mtd->oobsize - i * j);
	}
}

static void mxc_do_addr_cycle(struct mtd_info *mtd, int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	/* Write out column address, if necessary */
	if (column != -1) {
		/*
		 * MXC NANDFC can only perform full page+spare or
		 * spare-only read/write.  When the upper layers
		 * layers perform a read/write buf operation,
		 * we will used the saved column address to index into
		 * the full page.
		 */
		host->send_addr(host, 0, page_addr == -1);
		if (mtd->writesize > 512)
			/* another col addr cycle for 2k page */
			host->send_addr(host, 0, false);
	}

	/* Write out page address, if necessary */
	if (page_addr != -1) {
		/* paddr_0 - p_addr_7 */
		host->send_addr(host, (page_addr & 0xff), false);

		if (mtd->writesize > 512) {
			if (mtd->size >= 0x10000000) {
				/* paddr_8 - paddr_15 */
				host->send_addr(host, (page_addr >> 8) & 0xff, false);
				host->send_addr(host, (page_addr >> 16) & 0xff, true);
			} else
				/* paddr_8 - paddr_15 */
				host->send_addr(host, (page_addr >> 8) & 0xff, true);
		} else {
			/* One more address cycle for higher density devices */
			if (mtd->size >= 0x4000000) {
				/* paddr_8 - paddr_15 */
				host->send_addr(host, (page_addr >> 8) & 0xff, false);
				host->send_addr(host, (page_addr >> 16) & 0xff, true);
			} else
				/* paddr_8 - paddr_15 */
				host->send_addr(host, (page_addr >> 8) & 0xff, true);
		}
	}
}

static void preset_v1_v2(struct mtd_info *mtd)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;
	uint16_t tmp;

	/* enable interrupt, disable spare enable */
	tmp = readw(NFC_V1_V2_CONFIG1);
	tmp &= ~NFC_V1_V2_CONFIG1_INT_MSK;
	tmp &= ~NFC_V1_V2_CONFIG1_SP_EN;
	if (nand_chip->ecc.mode == NAND_ECC_HW) {
		tmp |= NFC_V1_V2_CONFIG1_ECC_EN;
	} else {
		tmp &= ~NFC_V1_V2_CONFIG1_ECC_EN;
	}
	writew(tmp, NFC_V1_V2_CONFIG1);
	/* preset operation */

	/* Unlock the internal RAM Buffer */
	writew(0x2, NFC_V1_V2_CONFIG);

	/* Blocks to be unlocked */
	if (nfc_is_v21()) {
		writew(0x0, NFC_V21_UNLOCKSTART_BLKADDR);
		writew(0xffff, NFC_V21_UNLOCKEND_BLKADDR);
	} else if (nfc_is_v1()) {
		writew(0x0, NFC_V1_UNLOCKSTART_BLKADDR);
		writew(0x4000, NFC_V1_UNLOCKEND_BLKADDR);
	} else
		BUG();

	/* Unlock Block Command for given address range */
	writew(0x4, NFC_V1_V2_WRPROT);
}

/* Used by the upper layer to write command to NAND Flash for
 * different operations to be carried out on NAND Flash */
static void mxc_nand_command(struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	struct nand_chip *nand_chip = mtd->priv;
	struct mxc_nand_host *host = nand_chip->priv;

	DEBUG(MTD_DEBUG_LEVEL3,
	      "mxc_nand_command (cmd = 0x%x, col = 0x%x, page = 0x%x)\n",
	      command, column, page_addr);

	/* Reset command state information */
	host->status_request = false;

	/* Command pre-processing step */
	switch (command) {
	case NAND_CMD_RESET:
		host->preset(mtd);
		host->send_cmd(host, command, false);
		break;

	case NAND_CMD_STATUS:
		host->buf_start = 0;
		host->status_request = true;

		host->send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READ0:
	case NAND_CMD_READOOB:
		if (command == NAND_CMD_READ0)
			host->buf_start = column;
		else
			host->buf_start = column + mtd->writesize;

		command = NAND_CMD_READ0; /* only READ0 is valid */

		host->send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);

		if (mtd->writesize > 512)
			host->send_cmd(host, NAND_CMD_READSTART, true);

		host->send_page(mtd, NFC_OUTPUT);

		memcpy(host->data_buf, host->main_area0, mtd->writesize);
		copy_spare(mtd, true);
		break;

	case NAND_CMD_SEQIN:
		if (column >= mtd->writesize)
			/* call ourself to read a page */
			mxc_nand_command(mtd, NAND_CMD_READ0, 0, page_addr);

		host->buf_start = column;

		host->send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_PAGEPROG:
		memcpy(host->main_area0, host->data_buf, mtd->writesize);
		copy_spare(mtd, false);
		host->send_page(mtd, NFC_INPUT);
		host->send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		break;

	case NAND_CMD_READID:
		host->send_cmd(host, command, true);
		mxc_do_addr_cycle(mtd, column, page_addr);
		host->send_read_id(host);
		host->buf_start = column;
		break;

	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		host->send_cmd(host, command, false);
		mxc_do_addr_cycle(mtd, column, page_addr);

		break;
	}
}

/*
 * The generic flash bbt decriptors overlap with our ecc
 * hardware, so define some i.MX specific ones.
 */
static uint8_t bbt_pattern[] = { 'B', 'b', 't', '0' };
static uint8_t mirror_pattern[] = { '1', 't', 'b', 'B' };

static struct nand_bbt_descr bbt_main_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = bbt_pattern,
};

static struct nand_bbt_descr bbt_mirror_descr = {
	.options = NAND_BBT_LASTBLOCK | NAND_BBT_CREATE | NAND_BBT_WRITE
	    | NAND_BBT_2BIT | NAND_BBT_VERSION | NAND_BBT_PERCHIP,
	.offs = 0,
	.len = 4,
	.veroffs = 4,
	.maxblocks = 4,
	.pattern = mirror_pattern,
};

static int __init mxcnd_probe(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct mtd_info *mtd;
	struct mxc_nand_platform_data *pdata = pdev->dev.platform_data;
	struct mxc_nand_host *host;
	struct resource *res;
	int err = 0, nr_parts = 0;
	struct nand_ecclayout *oob_smallpage, *oob_largepage;

	/* Allocate memory for MTD device structure and private data */
	host = kzalloc(sizeof(struct mxc_nand_host) + NAND_MAX_PAGESIZE +
			NAND_MAX_OOBSIZE, GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->data_buf = (uint8_t *)(host + 1);

	host->dev = &pdev->dev;
	/* structures must be linked */
	this = &host->nand;
	mtd = &host->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;
	mtd->name = DRIVER_NAME;

	/* 50 us command delay time */
	this->chip_delay = 5;

	this->priv = host;
	this->dev_ready = mxc_nand_dev_ready;
	this->cmdfunc = mxc_nand_command;
	this->select_chip = mxc_nand_select_chip;
	this->read_byte = mxc_nand_read_byte;
	this->read_word = mxc_nand_read_word;
	this->write_buf = mxc_nand_write_buf;
	this->read_buf = mxc_nand_read_buf;
	this->verify_buf = mxc_nand_verify_buf;

	host->clk = clk_get(&pdev->dev, "nfc");
	if (IS_ERR(host->clk)) {
		err = PTR_ERR(host->clk);
		goto eclk;
	}

	clk_enable(host->clk);
	host->clk_act = 1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto eres;
	}

	host->base = ioremap(res->start, resource_size(res));
	if (!host->base) {
		err = -ENOMEM;
		goto eres;
	}

	host->main_area0 = host->base;

	if (nfc_is_v1() || nfc_is_v21()) {
		host->preset = preset_v1_v2;
		host->send_cmd = send_cmd_v1_v2;
		host->send_addr = send_addr_v1_v2;
		host->send_page = send_page_v1_v2;
		host->send_read_id = send_read_id_v1_v2;
		host->get_dev_status = get_dev_status_v1_v2;
		host->check_int = check_int_v1_v2;
	}

	if (nfc_is_v21()) {
		host->regs = host->base + 0x1e00;
		host->spare0 = host->base + 0x1000;
		host->spare_len = 64;
		oob_smallpage = &nandv2_hw_eccoob_smallpage;
		oob_largepage = &nandv2_hw_eccoob_largepage;
		this->ecc.bytes = 9;
	} else if (nfc_is_v1()) {
		host->regs = host->base + 0xe00;
		host->spare0 = host->base + 0x800;
		host->spare_len = 16;
		oob_smallpage = &nandv1_hw_eccoob_smallpage;
		oob_largepage = &nandv1_hw_eccoob_largepage;
		this->ecc.bytes = 3;
	} else
		BUG();

	this->ecc.size = 512;
	this->ecc.layout = oob_smallpage;

	if (pdata->hw_ecc) {
		this->ecc.calculate = mxc_nand_calculate_ecc;
		this->ecc.hwctl = mxc_nand_enable_hwecc;
		if (nfc_is_v1())
			this->ecc.correct = mxc_nand_correct_data_v1;
		else
			this->ecc.correct = mxc_nand_correct_data_v2_v3;
		this->ecc.mode = NAND_ECC_HW;
	} else {
		this->ecc.mode = NAND_ECC_SOFT;
	}

	/* NAND bus width determines access funtions used by upper layer */
	if (pdata->width == 2)
		this->options |= NAND_BUSWIDTH_16;

	if (pdata->flash_bbt) {
		this->bbt_td = &bbt_main_descr;
		this->bbt_md = &bbt_mirror_descr;
		/* update flash based bbt */
		this->options |= NAND_USE_FLASH_BBT;
	}

	init_waitqueue_head(&host->irq_waitq);

	host->irq = platform_get_irq(pdev, 0);

	err = request_irq(host->irq, mxc_nfc_irq, IRQF_DISABLED, DRIVER_NAME, host);
	if (err)
		goto eirq;

	/* first scan to find the device and get the page size */
	if (nand_scan_ident(mtd, 1, NULL)) {
		err = -ENXIO;
		goto escan;
	}

	if (mtd->writesize == 2048)
		this->ecc.layout = oob_largepage;

	/* second phase scan */
	if (nand_scan_tail(mtd)) {
		err = -ENXIO;
		goto escan;
	}

	/* Register the partitions */
#ifdef CONFIG_MTD_PARTITIONS
	nr_parts =
	    parse_mtd_partitions(mtd, part_probes, &host->parts, 0);
	if (nr_parts > 0)
		add_mtd_partitions(mtd, host->parts, nr_parts);
	else
#endif
	{
		pr_info("Registering %s as whole device\n", mtd->name);
		add_mtd_device(mtd);
	}

	platform_set_drvdata(pdev, host);

	return 0;

escan:
	free_irq(host->irq, host);
eirq:
	iounmap(host->base);
eres:
	clk_put(host->clk);
eclk:
	kfree(host);

	return err;
}

static int __devexit mxcnd_remove(struct platform_device *pdev)
{
	struct mxc_nand_host *host = platform_get_drvdata(pdev);

	clk_put(host->clk);

	platform_set_drvdata(pdev, NULL);

	nand_release(&host->mtd);
	free_irq(host->irq, host);
	iounmap(host->base);
	kfree(host);

	return 0;
}

static struct platform_driver mxcnd_driver = {
	.driver = {
		   .name = DRIVER_NAME,
	},
	.remove = __devexit_p(mxcnd_remove),
};

static int __init mxc_nd_init(void)
{
	return platform_driver_probe(&mxcnd_driver, mxcnd_probe);
}

static void __exit mxc_nd_cleanup(void)
{
	/* Unregister the device structure */
	platform_driver_unregister(&mxcnd_driver);
}

module_init(mxc_nd_init);
module_exit(mxc_nd_cleanup);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC NAND MTD driver");
MODULE_LICENSE("GPL");
