/*
 * mrst.c: Intel Moorestown platform specific setup code
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sfi.h>
#include <linux/irq.h>

#include <asm/setup.h>
#include <asm/mpspec_def.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/mrst.h>
#include <asm/io.h>
#include <asm/i8259.h>

static u32 sfi_mtimer_usage[SFI_MTMR_MAX_NUM];
static struct sfi_timer_table_entry sfi_mtimer_array[SFI_MTMR_MAX_NUM];
int sfi_mtimer_num;

static inline void assign_to_mp_irq(struct mpc_intsrc *m,
				    struct mpc_intsrc *mp_irq)
{
	memcpy(mp_irq, m, sizeof(struct mpc_intsrc));
}

static inline int mp_irq_cmp(struct mpc_intsrc *mp_irq,
				struct mpc_intsrc *m)
{
	return memcmp(mp_irq, m, sizeof(struct mpc_intsrc));
}

static void save_mp_irq(struct mpc_intsrc *m)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		if (!mp_irq_cmp(&mp_irqs[i], m))
			return;
	}

	assign_to_mp_irq(m, &mp_irqs[mp_irq_entries]);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

/* parse all the mtimer info to a static mtimer array */
static int __init sfi_parse_mtmr(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_timer_table_entry *pentry;
	struct mpc_intsrc mp_irq;
	int totallen;

	sb = (struct sfi_table_simple *)table;
	if (!sfi_mtimer_num) {
		sfi_mtimer_num = SFI_GET_NUM_ENTRIES(sb,
					struct sfi_timer_table_entry);
		pentry = (struct sfi_timer_table_entry *) sb->pentry;
		totallen = sfi_mtimer_num * sizeof(*pentry);
		memcpy(sfi_mtimer_array, pentry, totallen);
	}

	printk(KERN_INFO "SFI: MTIMER info (num = %d):\n", sfi_mtimer_num);
	pentry = sfi_mtimer_array;
	for (totallen = 0; totallen < sfi_mtimer_num; totallen++, pentry++) {
		printk(KERN_INFO "timer[%d]: paddr = 0x%08x, freq = %dHz,"
			" irq = %d\n", totallen, (u32)pentry->phys_addr,
			pentry->freq_hz, pentry->irq);
			if (!pentry->irq)
				continue;
			mp_irq.type = MP_IOAPIC;
			mp_irq.irqtype = mp_INT;
/* triggering mode edge bit 2-3, active high polarity bit 0-1 */
			mp_irq.irqflag = 5;
			mp_irq.srcbus = 0;
			mp_irq.srcbusirq = pentry->irq;	/* IRQ */
			mp_irq.dstapic = MP_APIC_ALL;
			mp_irq.dstirq = pentry->irq;
			save_mp_irq(&mp_irq);
	}

	return 0;
}

struct sfi_timer_table_entry *sfi_get_mtmr(int hint)
{
	int i;
	if (hint < sfi_mtimer_num) {
		if (!sfi_mtimer_usage[hint]) {
			pr_debug("hint taken for timer %d irq %d\n",\
				hint, sfi_mtimer_array[hint].irq);
			sfi_mtimer_usage[hint] = 1;
			return &sfi_mtimer_array[hint];
		}
	}
	/* take the first timer available */
	for (i = 0; i < sfi_mtimer_num;) {
		if (!sfi_mtimer_usage[i]) {
			sfi_mtimer_usage[i] = 1;
			return &sfi_mtimer_array[i];
		}
		i++;
	}
	return NULL;
}

void sfi_free_mtmr(struct sfi_timer_table_entry *mtmr)
{
	int i;
	for (i = 0; i < sfi_mtimer_num;) {
		if (mtmr->irq == sfi_mtimer_array[i].irq) {
			sfi_mtimer_usage[i] = 0;
			return;
		}
		i++;
	}
}

/*
 * Moorestown specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_mrst_early_setup(void)
{
	x86_init.resources.probe_roms = x86_init_noop;
	x86_init.resources.reserve_resources = x86_init_noop;

	x86_init.pci.init = pci_mrst_init;
	x86_init.pci.fixup_irqs = x86_init_noop;

	legacy_pic = &null_legacy_pic;
}
