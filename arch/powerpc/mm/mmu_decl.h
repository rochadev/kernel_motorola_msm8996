/*
 * Declarations of procedures and variables shared between files
 * in arch/ppc/mm/.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */
#include <linux/mm.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>

extern void hash_preload(struct mm_struct *mm, unsigned long ea,
			 unsigned long access, unsigned long trap);


#ifdef CONFIG_PPC32
extern void mapin_ram(void);
extern int map_page(unsigned long va, phys_addr_t pa, int flags);
extern void setbat(int index, unsigned long virt, unsigned long phys,
		   unsigned int size, int flags);
extern void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		      unsigned int size, int flags, unsigned int pid);
extern void invalidate_tlbcam_entry(int index);

extern int __map_without_bats;
extern unsigned long ioremap_base;
extern unsigned int rtas_data, rtas_size;

extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;

extern unsigned int num_tlbcam_entries;
#endif

extern unsigned long ioremap_bot;
extern unsigned long __max_low_memory;
extern unsigned long __initial_memory_limit;
extern unsigned long total_memory;
extern unsigned long total_lowmem;

/* ...and now those things that may be slightly different between processor
 * architectures.  -- Dan
 */
#if defined(CONFIG_8xx)
#define flush_HPTE(X, va, pg)	_tlbie(va)
#define MMU_init_hw()		do { } while(0)
#define mmu_mapin_ram()		(0UL)

#elif defined(CONFIG_4xx)
#define flush_HPTE(X, va, pg)	_tlbie(va)
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);

#elif defined(CONFIG_FSL_BOOKE)
#define flush_HPTE(X, va, pg)	_tlbie(va)
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);
extern void adjust_total_lowmem(void);

#elif defined(CONFIG_PPC32)
/* anything 32-bit except 4xx or 8xx */
extern void MMU_init_hw(void);
extern unsigned long mmu_mapin_ram(void);

/* Be careful....this needs to be updated if we ever encounter 603 SMPs,
 * which includes all new 82xx processors.  We need tlbie/tlbsync here
 * in that case (I think). -- Dan.
 */
static inline void flush_HPTE(unsigned context, unsigned long va,
			      unsigned long pdval)
{
	if ((Hash != 0) &&
	    cpu_has_feature(CPU_FTR_HPTE_TABLE))
		flush_hash_pages(0, va, pdval, 1);
	else
		_tlbie(va);
}
#else /* CONFIG_PPC64 */
/* imalloc region types */
#define IM_REGION_UNUSED	0x1
#define IM_REGION_SUBSET	0x2
#define IM_REGION_EXISTS	0x4
#define IM_REGION_OVERLAP	0x8
#define IM_REGION_SUPERSET	0x10

extern struct vm_struct * im_get_free_area(unsigned long size);
extern struct vm_struct * im_get_area(unsigned long v_addr, unsigned long size,
				      int region_type);
extern void im_free(void *addr);
#endif
